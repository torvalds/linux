// SPDX-License-Identifier: GPL-2.0+
/*
 * The Huawei Cache Coherence System (HCCS) is a multi-chip interconnection
 * bus protocol.
 *
 * Copyright (c) 2023 Hisilicon Limited.
 * Author: Huisong Li <lihuisong@huawei.com>
 */
#include <linux/acpi.h>
#include <linux/iopoll.h>
#include <linux/platform_device.h>

#include <acpi/pcc.h>

#include "kunpeng_hccs.h"

/* PCC defines */
#define HCCS_PCC_SIGNATURE_MASK		0x50434300
#define HCCS_PCC_STATUS_CMD_COMPLETE	BIT(0)

/*
 * Arbitrary retries in case the remote processor is slow to respond
 * to PCC commands
 */
#define HCCS_PCC_CMD_WAIT_RETRIES_NUM		500ULL
#define HCCS_POLL_STATUS_TIME_INTERVAL_US	3

struct hccs_register_ctx {
	struct device *dev;
	u8 chan_id;
	int err;
};

static acpi_status hccs_get_register_cb(struct acpi_resource *ares,
					void *context)
{
	struct acpi_resource_generic_register *reg;
	struct hccs_register_ctx *ctx = context;

	if (ares->type != ACPI_RESOURCE_TYPE_GENERIC_REGISTER)
		return AE_OK;

	reg = &ares->data.generic_reg;
	if (reg->space_id != ACPI_ADR_SPACE_PLATFORM_COMM) {
		dev_err(ctx->dev, "Bad register resource.\n");
		ctx->err = -EINVAL;
		return AE_ERROR;
	}
	ctx->chan_id = reg->access_size;

	return AE_OK;
}

static int hccs_get_pcc_chan_id(struct hccs_dev *hdev)
{
	acpi_handle handle = ACPI_HANDLE(hdev->dev);
	struct hccs_register_ctx ctx = {0};
	acpi_status status;

	if (!acpi_has_method(handle, METHOD_NAME__CRS))
		return -ENODEV;

	ctx.dev = hdev->dev;
	status = acpi_walk_resources(handle, METHOD_NAME__CRS,
				     hccs_get_register_cb, &ctx);
	if (ACPI_FAILURE(status))
		return ctx.err;
	hdev->chan_id = ctx.chan_id;

	return 0;
}

static void hccs_chan_tx_done(struct mbox_client *cl, void *msg, int ret)
{
	if (ret < 0)
		pr_debug("TX did not complete: CMD sent:0x%x, ret:%d\n",
			 *(u8 *)msg, ret);
	else
		pr_debug("TX completed. CMD sent:0x%x, ret:%d\n",
			 *(u8 *)msg, ret);
}

static void hccs_unregister_pcc_channel(struct hccs_dev *hdev)
{
	struct hccs_mbox_client_info *cl_info = &hdev->cl_info;

	if (cl_info->pcc_comm_addr)
		iounmap(cl_info->pcc_comm_addr);
	pcc_mbox_free_channel(hdev->cl_info.pcc_chan);
}

static int hccs_register_pcc_channel(struct hccs_dev *hdev)
{
	struct hccs_mbox_client_info *cl_info = &hdev->cl_info;
	struct mbox_client *cl = &cl_info->client;
	struct pcc_mbox_chan *pcc_chan;
	struct device *dev = hdev->dev;
	int rc;

	cl->dev = dev;
	cl->tx_block = false;
	cl->knows_txdone = true;
	cl->tx_done = hccs_chan_tx_done;
	pcc_chan = pcc_mbox_request_channel(cl, hdev->chan_id);
	if (IS_ERR(pcc_chan)) {
		dev_err(dev, "PPC channel request failed.\n");
		rc = -ENODEV;
		goto out;
	}
	cl_info->pcc_chan = pcc_chan;
	cl_info->mbox_chan = pcc_chan->mchan;

	/*
	 * pcc_chan->latency is just a nominal value. In reality the remote
	 * processor could be much slower to reply. So add an arbitrary amount
	 * of wait on top of nominal.
	 */
	cl_info->deadline_us =
			HCCS_PCC_CMD_WAIT_RETRIES_NUM * pcc_chan->latency;
	if (cl_info->mbox_chan->mbox->txdone_irq) {
		dev_err(dev, "PCC IRQ in PCCT is enabled.\n");
		rc = -EINVAL;
		goto err_mbx_channel_free;
	}

	if (pcc_chan->shmem_base_addr) {
		cl_info->pcc_comm_addr = (void __force *)ioremap(
			pcc_chan->shmem_base_addr, pcc_chan->shmem_size);
		if (!cl_info->pcc_comm_addr) {
			dev_err(dev, "Failed to ioremap PCC communication region for channel-%d.\n",
				hdev->chan_id);
			rc = -ENOMEM;
			goto err_mbx_channel_free;
		}
	}

	return 0;

err_mbx_channel_free:
	pcc_mbox_free_channel(cl_info->pcc_chan);
out:
	return rc;
}

static int hccs_check_chan_cmd_complete(struct hccs_dev *hdev)
{
	struct hccs_mbox_client_info *cl_info = &hdev->cl_info;
	struct acpi_pcct_shared_memory *comm_base = cl_info->pcc_comm_addr;
	u16 status;
	int ret;

	/*
	 * Poll PCC status register every 3us(delay_us) for maximum of
	 * deadline_us(timeout_us) until PCC command complete bit is set(cond)
	 */
	ret = readw_poll_timeout(&comm_base->status, status,
				 status & HCCS_PCC_STATUS_CMD_COMPLETE,
				 HCCS_POLL_STATUS_TIME_INTERVAL_US,
				 cl_info->deadline_us);
	if (unlikely(ret))
		dev_err(hdev->dev, "poll PCC status failed, ret = %d.\n", ret);

	return ret;
}

static int hccs_pcc_cmd_send(struct hccs_dev *hdev, u8 cmd,
			     struct hccs_desc *desc)
{
	struct hccs_mbox_client_info *cl_info = &hdev->cl_info;
	struct acpi_pcct_shared_memory *comm_base = cl_info->pcc_comm_addr;
	void *comm_space = (void *)(comm_base + 1);
	struct hccs_fw_inner_head *fw_inner_head;
	struct acpi_pcct_shared_memory tmp = {0};
	u16 comm_space_size;
	int ret;

	/* Write signature for this subspace */
	tmp.signature = HCCS_PCC_SIGNATURE_MASK | hdev->chan_id;
	/* Write to the shared command region */
	tmp.command = cmd;
	/* Clear cmd complete bit */
	tmp.status = 0;
	memcpy_toio(comm_base, (void *)&tmp,
			sizeof(struct acpi_pcct_shared_memory));

	/* Copy the message to the PCC comm space */
	comm_space_size = HCCS_PCC_SHARE_MEM_BYTES -
				sizeof(struct acpi_pcct_shared_memory);
	memcpy_toio(comm_space, (void *)desc, comm_space_size);

	/* Ring doorbell */
	ret = mbox_send_message(cl_info->mbox_chan, &cmd);
	if (ret < 0) {
		dev_err(hdev->dev, "Send PCC mbox message failed, ret = %d.\n",
			ret);
		goto end;
	}

	/* Wait for completion */
	ret = hccs_check_chan_cmd_complete(hdev);
	if (ret)
		goto end;

	/* Copy response data */
	memcpy_fromio((void *)desc, comm_space, comm_space_size);
	fw_inner_head = &desc->rsp.fw_inner_head;
	if (fw_inner_head->retStatus) {
		dev_err(hdev->dev, "Execute PCC command failed, error code = %u.\n",
			fw_inner_head->retStatus);
		ret = -EIO;
	}

end:
	mbox_client_txdone(cl_info->mbox_chan, ret);
	return ret;
}

static void hccs_init_req_desc(struct hccs_desc *desc)
{
	struct hccs_req_desc *req = &desc->req;

	memset(desc, 0, sizeof(*desc));
	req->req_head.module_code = HCCS_SERDES_MODULE_CODE;
}

static int hccs_get_dev_caps(struct hccs_dev *hdev)
{
	struct hccs_desc desc;
	int ret;

	hccs_init_req_desc(&desc);
	ret = hccs_pcc_cmd_send(hdev, HCCS_GET_DEV_CAP, &desc);
	if (ret) {
		dev_err(hdev->dev, "Get device capabilities failed, ret = %d.\n",
			ret);
		return ret;
	}
	memcpy(&hdev->caps, desc.rsp.data, sizeof(hdev->caps));

	return 0;
}

static int hccs_query_chip_num_on_platform(struct hccs_dev *hdev)
{
	struct hccs_desc desc;
	int ret;

	hccs_init_req_desc(&desc);
	ret = hccs_pcc_cmd_send(hdev, HCCS_GET_CHIP_NUM, &desc);
	if (ret) {
		dev_err(hdev->dev, "query system chip number failed, ret = %d.\n",
			ret);
		return ret;
	}

	hdev->chip_num = *((u8 *)&desc.rsp.data);
	if (!hdev->chip_num) {
		dev_err(hdev->dev, "chip num obtained from firmware is zero.\n");
		return -EINVAL;
	}

	return 0;
}

static int hccs_get_chip_info(struct hccs_dev *hdev,
			      struct hccs_chip_info *chip)
{
	struct hccs_die_num_req_param *req_param;
	struct hccs_desc desc;
	int ret;

	hccs_init_req_desc(&desc);
	req_param = (struct hccs_die_num_req_param *)desc.req.data;
	req_param->chip_id = chip->chip_id;
	ret = hccs_pcc_cmd_send(hdev, HCCS_GET_DIE_NUM, &desc);
	if (ret)
		return ret;

	chip->die_num = *((u8 *)&desc.rsp.data);

	return 0;
}

static int hccs_query_chip_info_on_platform(struct hccs_dev *hdev)
{
	struct hccs_chip_info *chip;
	int ret;
	u8 idx;

	ret = hccs_query_chip_num_on_platform(hdev);
	if (ret) {
		dev_err(hdev->dev, "query chip number on platform failed, ret = %d.\n",
			ret);
		return ret;
	}

	hdev->chips = devm_kzalloc(hdev->dev,
				hdev->chip_num * sizeof(struct hccs_chip_info),
				GFP_KERNEL);
	if (!hdev->chips) {
		dev_err(hdev->dev, "allocate all chips memory failed.\n");
		return -ENOMEM;
	}

	for (idx = 0; idx < hdev->chip_num; idx++) {
		chip = &hdev->chips[idx];
		chip->chip_id = idx;
		ret = hccs_get_chip_info(hdev, chip);
		if (ret) {
			dev_err(hdev->dev, "get chip%u info failed, ret = %d.\n",
				idx, ret);
			return ret;
		}
		chip->hdev = hdev;
	}

	return 0;
}

static int hccs_query_die_info_on_chip(struct hccs_dev *hdev, u8 chip_id,
				       u8 die_idx, struct hccs_die_info *die)
{
	struct hccs_die_info_req_param *req_param;
	struct hccs_die_info_rsp_data *rsp_data;
	struct hccs_desc desc;
	int ret;

	hccs_init_req_desc(&desc);
	req_param = (struct hccs_die_info_req_param *)desc.req.data;
	req_param->chip_id = chip_id;
	req_param->die_idx = die_idx;
	ret = hccs_pcc_cmd_send(hdev, HCCS_GET_DIE_INFO, &desc);
	if (ret)
		return ret;

	rsp_data = (struct hccs_die_info_rsp_data *)desc.rsp.data;
	die->die_id = rsp_data->die_id;
	die->port_num = rsp_data->port_num;
	die->min_port_id = rsp_data->min_port_id;
	die->max_port_id = rsp_data->max_port_id;
	if (die->min_port_id > die->max_port_id) {
		dev_err(hdev->dev, "min port id(%u) > max port id(%u) on die_idx(%u).\n",
			die->min_port_id, die->max_port_id, die_idx);
		return -EINVAL;
	}
	if (die->max_port_id > HCCS_DIE_MAX_PORT_ID) {
		dev_err(hdev->dev, "max port id(%u) on die_idx(%u) is too big.\n",
			die->max_port_id, die_idx);
		return -EINVAL;
	}

	return 0;
}

static int hccs_query_all_die_info_on_platform(struct hccs_dev *hdev)
{
	struct device *dev = hdev->dev;
	struct hccs_chip_info *chip;
	struct hccs_die_info *die;
	u8 i, j;
	int ret;

	for (i = 0; i < hdev->chip_num; i++) {
		chip = &hdev->chips[i];
		if (!chip->die_num)
			continue;

		chip->dies = devm_kzalloc(hdev->dev,
				chip->die_num * sizeof(struct hccs_die_info),
				GFP_KERNEL);
		if (!chip->dies) {
			dev_err(dev, "allocate all dies memory on chip%u failed.\n",
				i);
			return -ENOMEM;
		}

		for (j = 0; j < chip->die_num; j++) {
			die = &chip->dies[j];
			ret = hccs_query_die_info_on_chip(hdev, i, j, die);
			if (ret) {
				dev_err(dev, "get die idx (%u) info on chip%u failed, ret = %d.\n",
					j, i, ret);
				return ret;
			}
			die->chip = chip;
		}
	}

	return 0;
}

static int hccs_get_bd_info(struct hccs_dev *hdev, u8 opcode,
			    struct hccs_desc *desc,
			    void *buf, size_t buf_len,
			    struct hccs_rsp_head *rsp_head)
{
	struct hccs_rsp_head *head;
	struct hccs_rsp_desc *rsp;
	int ret;

	ret = hccs_pcc_cmd_send(hdev, opcode, desc);
	if (ret)
		return ret;

	rsp = &desc->rsp;
	head = &rsp->rsp_head;
	if (head->data_len > buf_len) {
		dev_err(hdev->dev,
			"buffer overflow (buf_len = %lu, data_len = %u)!\n",
			buf_len, head->data_len);
		return -ENOMEM;
	}

	memcpy(buf, rsp->data, head->data_len);
	*rsp_head = *head;

	return 0;
}

static int hccs_get_all_port_attr(struct hccs_dev *hdev,
				  struct hccs_die_info *die,
				  struct hccs_port_attr *attrs, u16 size)
{
	struct hccs_die_comm_req_param *req_param;
	struct hccs_req_head *req_head;
	struct hccs_rsp_head rsp_head;
	struct hccs_desc desc;
	size_t left_buf_len;
	u32 data_len = 0;
	u8 start_id;
	u8 *buf;
	int ret;

	buf = (u8 *)attrs;
	left_buf_len = sizeof(struct hccs_port_attr) * size;
	start_id = die->min_port_id;
	while (start_id <= die->max_port_id) {
		hccs_init_req_desc(&desc);
		req_head = &desc.req.req_head;
		req_head->start_id = start_id;
		req_param = (struct hccs_die_comm_req_param *)desc.req.data;
		req_param->chip_id = die->chip->chip_id;
		req_param->die_id = die->die_id;

		ret = hccs_get_bd_info(hdev, HCCS_GET_DIE_PORT_INFO, &desc,
				       buf + data_len, left_buf_len, &rsp_head);
		if (ret) {
			dev_err(hdev->dev,
				"get the information of port%u on die%u failed, ret = %d.\n",
				start_id, die->die_id, ret);
			return ret;
		}

		data_len += rsp_head.data_len;
		left_buf_len -= rsp_head.data_len;
		if (unlikely(rsp_head.next_id <= start_id)) {
			dev_err(hdev->dev,
				"next port id (%u) is not greater than last start id (%u) on die%u.\n",
				rsp_head.next_id, start_id, die->die_id);
			return -EINVAL;
		}
		start_id = rsp_head.next_id;
	}

	return 0;
}

static int hccs_get_all_port_info_on_die(struct hccs_dev *hdev,
					 struct hccs_die_info *die)
{
	struct hccs_port_attr *attrs;
	struct hccs_port_info *port;
	int ret;
	u8 i;

	attrs = kcalloc(die->port_num, sizeof(struct hccs_port_attr),
			GFP_KERNEL);
	if (!attrs)
		return -ENOMEM;

	ret = hccs_get_all_port_attr(hdev, die, attrs, die->port_num);
	if (ret)
		goto out;

	for (i = 0; i < die->port_num; i++) {
		port = &die->ports[i];
		port->port_id = attrs[i].port_id;
		port->port_type = attrs[i].port_type;
		port->lane_mode = attrs[i].lane_mode;
		port->enable = attrs[i].enable;
		port->die = die;
	}

out:
	kfree(attrs);
	return ret;
}

static int hccs_query_all_port_info_on_platform(struct hccs_dev *hdev)
{

	struct device *dev = hdev->dev;
	struct hccs_chip_info *chip;
	struct hccs_die_info *die;
	u8 i, j;
	int ret;

	for (i = 0; i < hdev->chip_num; i++) {
		chip = &hdev->chips[i];
		for (j = 0; j < chip->die_num; j++) {
			die = &chip->dies[j];
			if (!die->port_num)
				continue;

			die->ports = devm_kzalloc(dev,
				die->port_num * sizeof(struct hccs_port_info),
				GFP_KERNEL);
			if (!die->ports) {
				dev_err(dev, "allocate ports memory on chip%u/die%u failed.\n",
					i, die->die_id);
				return -ENOMEM;
			}

			ret = hccs_get_all_port_info_on_die(hdev, die);
			if (ret) {
				dev_err(dev, "get all port info on chip%u/die%u failed, ret = %d.\n",
					i, die->die_id, ret);
				return ret;
			}
		}
	}

	return 0;
}

static int hccs_get_hw_info(struct hccs_dev *hdev)
{
	int ret;

	ret = hccs_query_chip_info_on_platform(hdev);
	if (ret) {
		dev_err(hdev->dev, "query chip info on platform failed, ret = %d.\n",
			ret);
		return ret;
	}

	ret = hccs_query_all_die_info_on_platform(hdev);
	if (ret) {
		dev_err(hdev->dev, "query all die info on platform failed, ret = %d.\n",
			ret);
		return ret;
	}

	ret = hccs_query_all_port_info_on_platform(hdev);
	if (ret) {
		dev_err(hdev->dev, "query all port info on platform failed, ret = %d.\n",
			ret);
		return ret;
	}

	return 0;
}

static int hccs_probe(struct platform_device *pdev)
{
	struct acpi_device *acpi_dev;
	struct hccs_dev *hdev;
	int rc;

	if (acpi_disabled) {
		dev_err(&pdev->dev, "acpi is disabled.\n");
		return -ENODEV;
	}
	acpi_dev = ACPI_COMPANION(&pdev->dev);
	if (!acpi_dev)
		return -ENODEV;

	hdev = devm_kzalloc(&pdev->dev, sizeof(*hdev), GFP_KERNEL);
	if (!hdev)
		return -ENOMEM;
	hdev->acpi_dev = acpi_dev;
	hdev->dev = &pdev->dev;
	platform_set_drvdata(pdev, hdev);

	mutex_init(&hdev->lock);
	rc = hccs_get_pcc_chan_id(hdev);
	if (rc)
		return rc;
	rc = hccs_register_pcc_channel(hdev);
	if (rc)
		return rc;

	rc = hccs_get_dev_caps(hdev);
	if (rc)
		goto unregister_pcc_chan;

	rc = hccs_get_hw_info(hdev);
	if (rc)
		goto unregister_pcc_chan;

	return 0;

unregister_pcc_chan:
	hccs_unregister_pcc_channel(hdev);

	return rc;
}

static int hccs_remove(struct platform_device *pdev)
{
	struct hccs_dev *hdev = platform_get_drvdata(pdev);

	hccs_unregister_pcc_channel(hdev);

	return 0;
}

static const struct acpi_device_id hccs_acpi_match[] = {
	{ "HISI04B1"},
	{ ""},
};
MODULE_DEVICE_TABLE(acpi, hccs_acpi_match);

static struct platform_driver hccs_driver = {
	.probe = hccs_probe,
	.remove = hccs_remove,
	.driver = {
		.name = "kunpeng_hccs",
		.acpi_match_table = hccs_acpi_match,
	},
};

module_platform_driver(hccs_driver);

MODULE_DESCRIPTION("Kunpeng SoC HCCS driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Huisong Li <lihuisong@huawei.com>");
