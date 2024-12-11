// SPDX-License-Identifier: GPL-2.0+
/*
 * The Huawei Cache Coherence System (HCCS) is a multi-chip interconnection
 * bus protocol.
 *
 * Copyright (c) 2023 Hisilicon Limited.
 * Author: Huisong Li <lihuisong@huawei.com>
 *
 * HCCS driver for Kunpeng SoC provides the following features:
 * - Retrieve the following information about each port:
 *    - port type
 *    - lane mode
 *    - enable
 *    - current lane mode
 *    - link finite state machine
 *    - lane mask
 *    - CRC error count
 *
 * - Retrieve the following information about all the ports on the chip or
 *   the die:
 *    - if all enabled ports are in linked
 *    - if all linked ports are in full lane
 *    - CRC error count sum
 *
 * - Retrieve all HCCS types used on the platform.
 *
 * - Support low power feature for all specified HCCS type ports, and
 *   provide the following interface:
 *    - query HCCS types supported increasing and decreasing lane number.
 *    - decrease lane number of all specified HCCS type ports on idle state.
 *    - increase lane number of all specified HCCS type ports.
 */
#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/iopoll.h>
#include <linux/platform_device.h>
#include <linux/stringify.h>
#include <linux/sysfs.h>
#include <linux/types.h>

#include <acpi/pcc.h>

#include "kunpeng_hccs.h"

/*
 * Arbitrary retries in case the remote processor is slow to respond
 * to PCC commands
 */
#define HCCS_PCC_CMD_WAIT_RETRIES_NUM		500ULL
#define HCCS_POLL_STATUS_TIME_INTERVAL_US	3

static struct hccs_port_info *kobj_to_port_info(struct kobject *k)
{
	return container_of(k, struct hccs_port_info, kobj);
}

static struct hccs_die_info *kobj_to_die_info(struct kobject *k)
{
	return container_of(k, struct hccs_die_info, kobj);
}

static struct hccs_chip_info *kobj_to_chip_info(struct kobject *k)
{
	return container_of(k, struct hccs_chip_info, kobj);
}

static struct hccs_dev *device_kobj_to_hccs_dev(struct kobject *k)
{
	struct device *dev = container_of(k, struct device, kobj);
	struct platform_device *pdev =
			container_of(dev, struct platform_device, dev);

	return platform_get_drvdata(pdev);
}

static char *hccs_port_type_to_name(struct hccs_dev *hdev, u8 type)
{
	u16 i;

	for (i = 0; i < hdev->used_type_num; i++) {
		if (hdev->type_name_maps[i].type == type)
			return hdev->type_name_maps[i].name;
	}

	return NULL;
}

static int hccs_name_to_port_type(struct hccs_dev *hdev,
				  const char *name, u8 *type)
{
	u16 i;

	for (i = 0; i < hdev->used_type_num; i++) {
		if (strcmp(hdev->type_name_maps[i].name, name) == 0) {
			*type = hdev->type_name_maps[i].type;
			return 0;
		}
	}

	return -EINVAL;
}

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

	if (!acpi_has_method(handle, METHOD_NAME__CRS)) {
		dev_err(hdev->dev, "No _CRS method.\n");
		return -ENODEV;
	}

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

static void hccs_pcc_rx_callback(struct mbox_client *cl, void *mssg)
{
	struct hccs_mbox_client_info *cl_info =
			container_of(cl, struct hccs_mbox_client_info, client);

	complete(&cl_info->done);
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
	cl->rx_callback = hdev->verspec_data->rx_callback;
	init_completion(&cl_info->done);

	pcc_chan = pcc_mbox_request_channel(cl, hdev->chan_id);
	if (IS_ERR(pcc_chan)) {
		dev_err(dev, "PCC channel request failed.\n");
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
	if (!hdev->verspec_data->has_txdone_irq &&
	    cl_info->mbox_chan->mbox->txdone_irq) {
		dev_err(dev, "PCC IRQ in PCCT is enabled.\n");
		rc = -EINVAL;
		goto err_mbx_channel_free;
	} else if (hdev->verspec_data->has_txdone_irq &&
		   !cl_info->mbox_chan->mbox->txdone_irq) {
		dev_err(dev, "PCC IRQ in PCCT isn't supported.\n");
		rc = -EINVAL;
		goto err_mbx_channel_free;
	}

	if (!pcc_chan->shmem_base_addr ||
	    pcc_chan->shmem_size != HCCS_PCC_SHARE_MEM_BYTES) {
		dev_err(dev, "The base address or size (%llu) of PCC communication region is invalid.\n",
			pcc_chan->shmem_size);
		rc = -EINVAL;
		goto err_mbx_channel_free;
	}

	cl_info->pcc_comm_addr = ioremap(pcc_chan->shmem_base_addr,
					 pcc_chan->shmem_size);
	if (!cl_info->pcc_comm_addr) {
		dev_err(dev, "Failed to ioremap PCC communication region for channel-%u.\n",
			hdev->chan_id);
		rc = -ENOMEM;
		goto err_mbx_channel_free;
	}

	return 0;

err_mbx_channel_free:
	pcc_mbox_free_channel(cl_info->pcc_chan);
out:
	return rc;
}

static int hccs_wait_cmd_complete_by_poll(struct hccs_dev *hdev)
{
	struct hccs_mbox_client_info *cl_info = &hdev->cl_info;
	struct acpi_pcct_shared_memory __iomem *comm_base =
							cl_info->pcc_comm_addr;
	u16 status;
	int ret;

	/*
	 * Poll PCC status register every 3us(delay_us) for maximum of
	 * deadline_us(timeout_us) until PCC command complete bit is set(cond)
	 */
	ret = readw_poll_timeout(&comm_base->status, status,
				 status & PCC_STATUS_CMD_COMPLETE,
				 HCCS_POLL_STATUS_TIME_INTERVAL_US,
				 cl_info->deadline_us);
	if (unlikely(ret))
		dev_err(hdev->dev, "poll PCC status failed, ret = %d.\n", ret);

	return ret;
}

static int hccs_wait_cmd_complete_by_irq(struct hccs_dev *hdev)
{
	struct hccs_mbox_client_info *cl_info = &hdev->cl_info;

	if (!wait_for_completion_timeout(&cl_info->done,
			usecs_to_jiffies(cl_info->deadline_us))) {
		dev_err(hdev->dev, "PCC command executed timeout!\n");
		return -ETIMEDOUT;
	}

	return 0;
}

static inline void hccs_fill_pcc_shared_mem_region(struct hccs_dev *hdev,
						   u8 cmd,
						   struct hccs_desc *desc,
						   void __iomem *comm_space,
						   u16 space_size)
{
	struct acpi_pcct_shared_memory tmp = {
		.signature = PCC_SIGNATURE | hdev->chan_id,
		.command = cmd,
		.status = 0,
	};

	memcpy_toio(hdev->cl_info.pcc_comm_addr, (void *)&tmp,
		    sizeof(struct acpi_pcct_shared_memory));

	/* Copy the message to the PCC comm space */
	memcpy_toio(comm_space, (void *)desc, space_size);
}

static inline void hccs_fill_ext_pcc_shared_mem_region(struct hccs_dev *hdev,
						       u8 cmd,
						       struct hccs_desc *desc,
						       void __iomem *comm_space,
						       u16 space_size)
{
	struct acpi_pcct_ext_pcc_shared_memory tmp = {
		.signature = PCC_SIGNATURE | hdev->chan_id,
		.flags = PCC_CMD_COMPLETION_NOTIFY,
		.length = HCCS_PCC_SHARE_MEM_BYTES,
		.command = cmd,
	};

	memcpy_toio(hdev->cl_info.pcc_comm_addr, (void *)&tmp,
		    sizeof(struct acpi_pcct_ext_pcc_shared_memory));

	/* Copy the message to the PCC comm space */
	memcpy_toio(comm_space, (void *)desc, space_size);
}

static int hccs_pcc_cmd_send(struct hccs_dev *hdev, u8 cmd,
			     struct hccs_desc *desc)
{
	const struct hccs_verspecific_data *verspec_data = hdev->verspec_data;
	struct hccs_mbox_client_info *cl_info = &hdev->cl_info;
	struct hccs_fw_inner_head *fw_inner_head;
	void __iomem *comm_space;
	u16 space_size;
	int ret;

	comm_space = cl_info->pcc_comm_addr + verspec_data->shared_mem_size;
	space_size = HCCS_PCC_SHARE_MEM_BYTES - verspec_data->shared_mem_size;
	verspec_data->fill_pcc_shared_mem(hdev, cmd, desc,
					  comm_space, space_size);
	if (verspec_data->has_txdone_irq)
		reinit_completion(&cl_info->done);

	/* Ring doorbell */
	ret = mbox_send_message(cl_info->mbox_chan, &cmd);
	if (ret < 0) {
		dev_err(hdev->dev, "Send PCC mbox message failed, ret = %d.\n",
			ret);
		goto end;
	}

	ret = verspec_data->wait_cmd_complete(hdev);
	if (ret)
		goto end;

	/* Copy response data */
	memcpy_fromio((void *)desc, comm_space, space_size);
	fw_inner_head = &desc->rsp.fw_inner_head;
	if (fw_inner_head->retStatus) {
		dev_err(hdev->dev, "Execute PCC command failed, error code = %u.\n",
			fw_inner_head->retStatus);
		ret = -EIO;
	}

end:
	if (verspec_data->has_txdone_irq)
		mbox_chan_txdone(cl_info->mbox_chan, ret);
	else
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
	bool has_die_info = false;
	u8 i, j;
	int ret;

	for (i = 0; i < hdev->chip_num; i++) {
		chip = &hdev->chips[i];
		if (!chip->die_num)
			continue;

		has_die_info = true;
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

	return has_die_info ? 0 : -EINVAL;
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
			"buffer overflow (buf_len = %zu, data_len = %u)!\n",
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

	if (left_buf_len != 0) {
		dev_err(hdev->dev, "failed to get the expected port number(%u) attribute.\n",
			size);
		return -EINVAL;
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
		port->max_lane_num = attrs[i].max_lane_num;
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
	bool has_port_info = false;
	u8 i, j;
	int ret;

	for (i = 0; i < hdev->chip_num; i++) {
		chip = &hdev->chips[i];
		for (j = 0; j < chip->die_num; j++) {
			die = &chip->dies[j];
			if (!die->port_num)
				continue;

			has_port_info = true;
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

	return has_port_info ? 0 : -EINVAL;
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

static u16 hccs_calc_used_type_num(struct hccs_dev *hdev,
				   unsigned long *hccs_ver)
{
	struct hccs_chip_info *chip;
	struct hccs_port_info *port;
	struct hccs_die_info *die;
	u16 used_type_num = 0;
	u16 i, j, k;

	for (i = 0; i < hdev->chip_num; i++) {
		chip = &hdev->chips[i];
		for (j = 0; j < chip->die_num; j++) {
			die = &chip->dies[j];
			for (k = 0; k < die->port_num; k++) {
				port = &die->ports[k];
				set_bit(port->port_type, hccs_ver);
			}
		}
	}

	for_each_set_bit(i, hccs_ver, HCCS_IP_MAX + 1)
		used_type_num++;

	return used_type_num;
}

static int hccs_init_type_name_maps(struct hccs_dev *hdev)
{
	DECLARE_BITMAP(hccs_ver, HCCS_IP_MAX + 1) = {};
	unsigned int i;
	u16 idx = 0;

	hdev->used_type_num = hccs_calc_used_type_num(hdev, hccs_ver);
	hdev->type_name_maps = devm_kcalloc(hdev->dev, hdev->used_type_num,
					    sizeof(struct hccs_type_name_map),
					    GFP_KERNEL);
	if (!hdev->type_name_maps)
		return -ENOMEM;

	for_each_set_bit(i, hccs_ver, HCCS_IP_MAX + 1) {
		hdev->type_name_maps[idx].type = i;
		sprintf(hdev->type_name_maps[idx].name,
			"%s%u", HCCS_IP_PREFIX, i);
		idx++;
	}

	return 0;
}

static int hccs_query_port_link_status(struct hccs_dev *hdev,
				       const struct hccs_port_info *port,
				       struct hccs_link_status *link_status)
{
	const struct hccs_die_info *die = port->die;
	const struct hccs_chip_info *chip = die->chip;
	struct hccs_port_comm_req_param *req_param;
	struct hccs_desc desc;
	int ret;

	hccs_init_req_desc(&desc);
	req_param = (struct hccs_port_comm_req_param *)desc.req.data;
	req_param->chip_id = chip->chip_id;
	req_param->die_id = die->die_id;
	req_param->port_id = port->port_id;
	ret = hccs_pcc_cmd_send(hdev, HCCS_GET_PORT_LINK_STATUS, &desc);
	if (ret) {
		dev_err(hdev->dev,
			"get port link status info failed, ret = %d.\n", ret);
		return ret;
	}

	*link_status = *((struct hccs_link_status *)desc.rsp.data);

	return 0;
}

static int hccs_query_port_crc_err_cnt(struct hccs_dev *hdev,
				       const struct hccs_port_info *port,
				       u64 *crc_err_cnt)
{
	const struct hccs_die_info *die = port->die;
	const struct hccs_chip_info *chip = die->chip;
	struct hccs_port_comm_req_param *req_param;
	struct hccs_desc desc;
	int ret;

	hccs_init_req_desc(&desc);
	req_param = (struct hccs_port_comm_req_param *)desc.req.data;
	req_param->chip_id = chip->chip_id;
	req_param->die_id = die->die_id;
	req_param->port_id = port->port_id;
	ret = hccs_pcc_cmd_send(hdev, HCCS_GET_PORT_CRC_ERR_CNT, &desc);
	if (ret) {
		dev_err(hdev->dev,
			"get port crc error count failed, ret = %d.\n", ret);
		return ret;
	}

	memcpy(crc_err_cnt, &desc.rsp.data, sizeof(u64));

	return 0;
}

static int hccs_get_die_all_link_status(struct hccs_dev *hdev,
					const struct hccs_die_info *die,
					u8 *all_linked)
{
	struct hccs_die_comm_req_param *req_param;
	struct hccs_desc desc;
	int ret;

	if (die->port_num == 0) {
		*all_linked = 1;
		return 0;
	}

	hccs_init_req_desc(&desc);
	req_param = (struct hccs_die_comm_req_param *)desc.req.data;
	req_param->chip_id = die->chip->chip_id;
	req_param->die_id = die->die_id;
	ret = hccs_pcc_cmd_send(hdev, HCCS_GET_DIE_PORTS_LINK_STA, &desc);
	if (ret) {
		dev_err(hdev->dev,
			"get link status of all ports failed on die%u, ret = %d.\n",
			die->die_id, ret);
		return ret;
	}

	*all_linked = *((u8 *)&desc.rsp.data);

	return 0;
}

static int hccs_get_die_all_port_lane_status(struct hccs_dev *hdev,
					     const struct hccs_die_info *die,
					     u8 *full_lane)
{
	struct hccs_die_comm_req_param *req_param;
	struct hccs_desc desc;
	int ret;

	if (die->port_num == 0) {
		*full_lane = 1;
		return 0;
	}

	hccs_init_req_desc(&desc);
	req_param = (struct hccs_die_comm_req_param *)desc.req.data;
	req_param->chip_id = die->chip->chip_id;
	req_param->die_id = die->die_id;
	ret = hccs_pcc_cmd_send(hdev, HCCS_GET_DIE_PORTS_LANE_STA, &desc);
	if (ret) {
		dev_err(hdev->dev, "get lane status of all ports failed on die%u, ret = %d.\n",
			die->die_id, ret);
		return ret;
	}

	*full_lane = *((u8 *)&desc.rsp.data);

	return 0;
}

static int hccs_get_die_total_crc_err_cnt(struct hccs_dev *hdev,
					  const struct hccs_die_info *die,
					  u64 *total_crc_err_cnt)
{
	struct hccs_die_comm_req_param *req_param;
	struct hccs_desc desc;
	int ret;

	if (die->port_num == 0) {
		*total_crc_err_cnt = 0;
		return 0;
	}

	hccs_init_req_desc(&desc);
	req_param = (struct hccs_die_comm_req_param *)desc.req.data;
	req_param->chip_id = die->chip->chip_id;
	req_param->die_id = die->die_id;
	ret = hccs_pcc_cmd_send(hdev, HCCS_GET_DIE_PORTS_CRC_ERR_CNT, &desc);
	if (ret) {
		dev_err(hdev->dev, "get crc error count sum failed on die%u, ret = %d.\n",
			die->die_id, ret);
		return ret;
	}

	memcpy(total_crc_err_cnt, &desc.rsp.data, sizeof(u64));

	return 0;
}

static ssize_t hccs_show(struct kobject *k, struct attribute *attr, char *buf)
{
	struct kobj_attribute *kobj_attr;

	kobj_attr = container_of(attr, struct kobj_attribute, attr);

	return kobj_attr->show(k, kobj_attr, buf);
}

static const struct sysfs_ops hccs_comm_ops = {
	.show = hccs_show,
};

static ssize_t type_show(struct kobject *kobj, struct kobj_attribute *attr,
			 char *buf)
{
	const struct hccs_port_info *port = kobj_to_port_info(kobj);

	return sysfs_emit(buf, "%s%u\n", HCCS_IP_PREFIX, port->port_type);
}
static struct kobj_attribute hccs_type_attr = __ATTR_RO(type);

static ssize_t lane_mode_show(struct kobject *kobj, struct kobj_attribute *attr,
			      char *buf)
{
	const struct hccs_port_info *port = kobj_to_port_info(kobj);

	return sysfs_emit(buf, "x%u\n", port->max_lane_num);
}
static struct kobj_attribute lane_mode_attr = __ATTR_RO(lane_mode);

static ssize_t enable_show(struct kobject *kobj,
				 struct kobj_attribute *attr, char *buf)
{
	const struct hccs_port_info *port = kobj_to_port_info(kobj);

	return sysfs_emit(buf, "%u\n", port->enable);
}
static struct kobj_attribute port_enable_attr = __ATTR_RO(enable);

static ssize_t cur_lane_num_show(struct kobject *kobj,
				 struct kobj_attribute *attr, char *buf)
{
	const struct hccs_port_info *port = kobj_to_port_info(kobj);
	struct hccs_dev *hdev = port->die->chip->hdev;
	struct hccs_link_status link_status = {0};
	int ret;

	mutex_lock(&hdev->lock);
	ret = hccs_query_port_link_status(hdev, port, &link_status);
	mutex_unlock(&hdev->lock);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%u\n", link_status.lane_num);
}
static struct kobj_attribute cur_lane_num_attr = __ATTR_RO(cur_lane_num);

static ssize_t link_fsm_show(struct kobject *kobj,
			     struct kobj_attribute *attr, char *buf)
{
	const struct hccs_port_info *port = kobj_to_port_info(kobj);
	struct hccs_dev *hdev = port->die->chip->hdev;
	struct hccs_link_status link_status = {0};
	const struct {
		u8 link_fsm;
		char *str;
	} link_fsm_map[] = {
		{HCCS_PORT_RESET, "reset"},
		{HCCS_PORT_SETUP, "setup"},
		{HCCS_PORT_CONFIG, "config"},
		{HCCS_PORT_READY, "link-up"},
	};
	const char *link_fsm_str = "unknown";
	size_t i;
	int ret;

	mutex_lock(&hdev->lock);
	ret = hccs_query_port_link_status(hdev, port, &link_status);
	mutex_unlock(&hdev->lock);
	if (ret)
		return ret;

	for (i = 0; i < ARRAY_SIZE(link_fsm_map); i++) {
		if (link_fsm_map[i].link_fsm == link_status.link_fsm) {
			link_fsm_str = link_fsm_map[i].str;
			break;
		}
	}

	return sysfs_emit(buf, "%s\n", link_fsm_str);
}
static struct kobj_attribute link_fsm_attr = __ATTR_RO(link_fsm);

static ssize_t lane_mask_show(struct kobject *kobj,
			      struct kobj_attribute *attr, char *buf)
{
	const struct hccs_port_info *port = kobj_to_port_info(kobj);
	struct hccs_dev *hdev = port->die->chip->hdev;
	struct hccs_link_status link_status = {0};
	int ret;

	mutex_lock(&hdev->lock);
	ret = hccs_query_port_link_status(hdev, port, &link_status);
	mutex_unlock(&hdev->lock);
	if (ret)
		return ret;

	return sysfs_emit(buf, "0x%x\n", link_status.lane_mask);
}
static struct kobj_attribute lane_mask_attr = __ATTR_RO(lane_mask);

static ssize_t crc_err_cnt_show(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf)
{
	const struct hccs_port_info *port = kobj_to_port_info(kobj);
	struct hccs_dev *hdev = port->die->chip->hdev;
	u64 crc_err_cnt;
	int ret;

	mutex_lock(&hdev->lock);
	ret = hccs_query_port_crc_err_cnt(hdev, port, &crc_err_cnt);
	mutex_unlock(&hdev->lock);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%llu\n", crc_err_cnt);
}
static struct kobj_attribute crc_err_cnt_attr = __ATTR_RO(crc_err_cnt);

static struct attribute *hccs_port_default_attrs[] = {
	&hccs_type_attr.attr,
	&lane_mode_attr.attr,
	&port_enable_attr.attr,
	&cur_lane_num_attr.attr,
	&link_fsm_attr.attr,
	&lane_mask_attr.attr,
	&crc_err_cnt_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(hccs_port_default);

static const struct kobj_type hccs_port_type = {
	.sysfs_ops = &hccs_comm_ops,
	.default_groups = hccs_port_default_groups,
};

static ssize_t all_linked_on_die_show(struct kobject *kobj,
				      struct kobj_attribute *attr, char *buf)
{
	const struct hccs_die_info *die = kobj_to_die_info(kobj);
	struct hccs_dev *hdev = die->chip->hdev;
	u8 all_linked;
	int ret;

	mutex_lock(&hdev->lock);
	ret = hccs_get_die_all_link_status(hdev, die, &all_linked);
	mutex_unlock(&hdev->lock);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%u\n", all_linked);
}
static struct kobj_attribute all_linked_on_die_attr =
		__ATTR(all_linked, 0444, all_linked_on_die_show, NULL);

static ssize_t linked_full_lane_on_die_show(struct kobject *kobj,
					    struct kobj_attribute *attr,
					    char *buf)
{
	const struct hccs_die_info *die = kobj_to_die_info(kobj);
	struct hccs_dev *hdev = die->chip->hdev;
	u8 full_lane;
	int ret;

	mutex_lock(&hdev->lock);
	ret = hccs_get_die_all_port_lane_status(hdev, die, &full_lane);
	mutex_unlock(&hdev->lock);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%u\n", full_lane);
}
static struct kobj_attribute linked_full_lane_on_die_attr =
	__ATTR(linked_full_lane, 0444, linked_full_lane_on_die_show, NULL);

static ssize_t crc_err_cnt_sum_on_die_show(struct kobject *kobj,
					   struct kobj_attribute *attr,
					   char *buf)
{
	const struct hccs_die_info *die = kobj_to_die_info(kobj);
	struct hccs_dev *hdev = die->chip->hdev;
	u64 total_crc_err_cnt;
	int ret;

	mutex_lock(&hdev->lock);
	ret = hccs_get_die_total_crc_err_cnt(hdev, die, &total_crc_err_cnt);
	mutex_unlock(&hdev->lock);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%llu\n", total_crc_err_cnt);
}
static struct kobj_attribute crc_err_cnt_sum_on_die_attr =
	__ATTR(crc_err_cnt, 0444, crc_err_cnt_sum_on_die_show, NULL);

static struct attribute *hccs_die_default_attrs[] = {
	&all_linked_on_die_attr.attr,
	&linked_full_lane_on_die_attr.attr,
	&crc_err_cnt_sum_on_die_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(hccs_die_default);

static const struct kobj_type hccs_die_type = {
	.sysfs_ops = &hccs_comm_ops,
	.default_groups = hccs_die_default_groups,
};

static ssize_t all_linked_on_chip_show(struct kobject *kobj,
				       struct kobj_attribute *attr, char *buf)
{
	const struct hccs_chip_info *chip = kobj_to_chip_info(kobj);
	struct hccs_dev *hdev = chip->hdev;
	const struct hccs_die_info *die;
	u8 all_linked = 1;
	u8 i, tmp;
	int ret;

	mutex_lock(&hdev->lock);
	for (i = 0; i < chip->die_num; i++) {
		die = &chip->dies[i];
		ret = hccs_get_die_all_link_status(hdev, die, &tmp);
		if (ret) {
			mutex_unlock(&hdev->lock);
			return ret;
		}
		if (tmp != all_linked) {
			all_linked = 0;
			break;
		}
	}
	mutex_unlock(&hdev->lock);

	return sysfs_emit(buf, "%u\n", all_linked);
}
static struct kobj_attribute all_linked_on_chip_attr =
		__ATTR(all_linked, 0444, all_linked_on_chip_show, NULL);

static ssize_t linked_full_lane_on_chip_show(struct kobject *kobj,
					     struct kobj_attribute *attr,
					     char *buf)
{
	const struct hccs_chip_info *chip = kobj_to_chip_info(kobj);
	struct hccs_dev *hdev = chip->hdev;
	const struct hccs_die_info *die;
	u8 full_lane = 1;
	u8 i, tmp;
	int ret;

	mutex_lock(&hdev->lock);
	for (i = 0; i < chip->die_num; i++) {
		die = &chip->dies[i];
		ret = hccs_get_die_all_port_lane_status(hdev, die, &tmp);
		if (ret) {
			mutex_unlock(&hdev->lock);
			return ret;
		}
		if (tmp != full_lane) {
			full_lane = 0;
			break;
		}
	}
	mutex_unlock(&hdev->lock);

	return sysfs_emit(buf, "%u\n", full_lane);
}
static struct kobj_attribute linked_full_lane_on_chip_attr =
	__ATTR(linked_full_lane, 0444, linked_full_lane_on_chip_show, NULL);

static ssize_t crc_err_cnt_sum_on_chip_show(struct kobject *kobj,
					    struct kobj_attribute *attr,
					    char *buf)
{
	const struct hccs_chip_info *chip = kobj_to_chip_info(kobj);
	u64 crc_err_cnt, total_crc_err_cnt = 0;
	struct hccs_dev *hdev = chip->hdev;
	const struct hccs_die_info *die;
	int ret;
	u16 i;

	mutex_lock(&hdev->lock);
	for (i = 0; i < chip->die_num; i++) {
		die = &chip->dies[i];
		ret = hccs_get_die_total_crc_err_cnt(hdev, die, &crc_err_cnt);
		if (ret) {
			mutex_unlock(&hdev->lock);
			return ret;
		}

		total_crc_err_cnt += crc_err_cnt;
	}
	mutex_unlock(&hdev->lock);

	return sysfs_emit(buf, "%llu\n", total_crc_err_cnt);
}
static struct kobj_attribute crc_err_cnt_sum_on_chip_attr =
		__ATTR(crc_err_cnt, 0444, crc_err_cnt_sum_on_chip_show, NULL);

static struct attribute *hccs_chip_default_attrs[] = {
	&all_linked_on_chip_attr.attr,
	&linked_full_lane_on_chip_attr.attr,
	&crc_err_cnt_sum_on_chip_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(hccs_chip_default);

static const struct kobj_type hccs_chip_type = {
	.sysfs_ops = &hccs_comm_ops,
	.default_groups = hccs_chip_default_groups,
};

static int hccs_parse_pm_port_type(struct hccs_dev *hdev, const char *buf,
				   u8 *port_type)
{
	char hccs_name[HCCS_NAME_MAX_LEN + 1] = "";
	u8 type;
	int ret;

	ret = sscanf(buf, "%" __stringify(HCCS_NAME_MAX_LEN) "s", hccs_name);
	if (ret != 1)
		return -EINVAL;

	ret = hccs_name_to_port_type(hdev, hccs_name, &type);
	if (ret) {
		dev_dbg(hdev->dev, "input invalid, please get the available types from 'used_types'.\n");
		return ret;
	}

	if (type == HCCS_V2 && hdev->caps & HCCS_CAPS_HCCS_V2_PM) {
		*port_type = type;
		return 0;
	}

	dev_dbg(hdev->dev, "%s doesn't support for increasing and decreasing lane.\n",
		hccs_name);

	return -EOPNOTSUPP;
}

static int hccs_query_port_idle_status(struct hccs_dev *hdev,
				       struct hccs_port_info *port, u8 *idle)
{
	const struct hccs_die_info *die = port->die;
	const struct hccs_chip_info *chip = die->chip;
	struct hccs_port_comm_req_param *req_param;
	struct hccs_desc desc;
	int ret;

	hccs_init_req_desc(&desc);
	req_param = (struct hccs_port_comm_req_param *)desc.req.data;
	req_param->chip_id = chip->chip_id;
	req_param->die_id = die->die_id;
	req_param->port_id = port->port_id;
	ret = hccs_pcc_cmd_send(hdev, HCCS_GET_PORT_IDLE_STATUS, &desc);
	if (ret) {
		dev_err(hdev->dev,
			"get port idle status failed, ret = %d.\n", ret);
		return ret;
	}

	*idle = *((u8 *)desc.rsp.data);
	return 0;
}

static int hccs_get_all_spec_port_idle_sta(struct hccs_dev *hdev, u8 port_type,
					   bool *all_idle)
{
	struct hccs_chip_info *chip;
	struct hccs_port_info *port;
	struct hccs_die_info *die;
	int ret = 0;
	u8 i, j, k;
	u8 idle;

	*all_idle = false;
	for (i = 0; i < hdev->chip_num; i++) {
		chip = &hdev->chips[i];
		for (j = 0; j < chip->die_num; j++) {
			die = &chip->dies[j];
			for (k = 0; k < die->port_num; k++) {
				port = &die->ports[k];
				if (port->port_type != port_type)
					continue;
				ret = hccs_query_port_idle_status(hdev, port,
								  &idle);
				if (ret) {
					dev_err(hdev->dev,
						"hccs%u on chip%u/die%u get idle status failed, ret = %d.\n",
						k, i, j, ret);
					return ret;
				} else if (idle == 0) {
					dev_info(hdev->dev, "hccs%u on chip%u/die%u is busy.\n",
						k, i, j);
					return 0;
				}
			}
		}
	}
	*all_idle = true;

	return 0;
}

static int hccs_get_all_spec_port_full_lane_sta(struct hccs_dev *hdev,
						u8 port_type, bool *full_lane)
{
	struct hccs_link_status status = {0};
	struct hccs_chip_info *chip;
	struct hccs_port_info *port;
	struct hccs_die_info *die;
	u8 i, j, k;
	int ret;

	*full_lane = false;
	for (i = 0; i < hdev->chip_num; i++) {
		chip = &hdev->chips[i];
		for (j = 0; j < chip->die_num; j++) {
			die = &chip->dies[j];
			for (k = 0; k < die->port_num; k++) {
				port = &die->ports[k];
				if (port->port_type != port_type)
					continue;
				ret = hccs_query_port_link_status(hdev, port,
								  &status);
				if (ret)
					return ret;
				if (status.lane_num != port->max_lane_num)
					return 0;
			}
		}
	}
	*full_lane = true;

	return 0;
}

static int hccs_prepare_inc_lane(struct hccs_dev *hdev, u8 type)
{
	struct hccs_inc_lane_req_param *req_param;
	struct hccs_desc desc;
	int ret;

	hccs_init_req_desc(&desc);
	req_param = (struct hccs_inc_lane_req_param *)desc.req.data;
	req_param->port_type = type;
	req_param->opt_type = HCCS_PREPARE_INC_LANE;
	ret = hccs_pcc_cmd_send(hdev, HCCS_PM_INC_LANE, &desc);
	if (ret)
		dev_err(hdev->dev, "prepare for increasing lane failed, ret = %d.\n",
			ret);

	return ret;
}

static int hccs_wait_serdes_adapt_completed(struct hccs_dev *hdev, u8 type)
{
#define HCCS_MAX_WAIT_CNT_FOR_ADAPT	10
#define HCCS_QUERY_ADAPT_RES_DELAY_MS	100
#define HCCS_SERDES_ADAPT_OK		0

	struct hccs_inc_lane_req_param *req_param;
	u8 wait_cnt = HCCS_MAX_WAIT_CNT_FOR_ADAPT;
	struct hccs_desc desc;
	u8 adapt_res;
	int ret;

	do {
		hccs_init_req_desc(&desc);
		req_param = (struct hccs_inc_lane_req_param *)desc.req.data;
		req_param->port_type = type;
		req_param->opt_type = HCCS_GET_ADAPT_RES;
		ret = hccs_pcc_cmd_send(hdev, HCCS_PM_INC_LANE, &desc);
		if (ret) {
			dev_err(hdev->dev, "query adapting result failed, ret = %d.\n",
				ret);
			return ret;
		}
		adapt_res = *((u8 *)&desc.rsp.data);
		if (adapt_res == HCCS_SERDES_ADAPT_OK)
			return 0;

		msleep(HCCS_QUERY_ADAPT_RES_DELAY_MS);
	} while (--wait_cnt);

	dev_err(hdev->dev, "wait for adapting completed timeout.\n");

	return -ETIMEDOUT;
}

static int hccs_start_hpcs_retraining(struct hccs_dev *hdev, u8 type)
{
	struct hccs_inc_lane_req_param *req_param;
	struct hccs_desc desc;
	int ret;

	hccs_init_req_desc(&desc);
	req_param = (struct hccs_inc_lane_req_param *)desc.req.data;
	req_param->port_type = type;
	req_param->opt_type = HCCS_START_RETRAINING;
	ret = hccs_pcc_cmd_send(hdev, HCCS_PM_INC_LANE, &desc);
	if (ret)
		dev_err(hdev->dev, "start hpcs retraining failed, ret = %d.\n",
			ret);

	return ret;
}

static int hccs_start_inc_lane(struct hccs_dev *hdev, u8 type)
{
	int ret;

	ret = hccs_prepare_inc_lane(hdev, type);
	if (ret)
		return ret;

	ret = hccs_wait_serdes_adapt_completed(hdev, type);
	if (ret)
		return ret;

	return hccs_start_hpcs_retraining(hdev, type);
}

static int hccs_start_dec_lane(struct hccs_dev *hdev, u8 type)
{
	struct hccs_desc desc;
	u8 *port_type;
	int ret;

	hccs_init_req_desc(&desc);
	port_type = (u8 *)desc.req.data;
	*port_type = type;
	ret = hccs_pcc_cmd_send(hdev, HCCS_PM_DEC_LANE, &desc);
	if (ret)
		dev_err(hdev->dev, "start to decrease lane failed, ret = %d.\n",
			ret);

	return ret;
}

static ssize_t dec_lane_of_type_store(struct kobject *kobj, struct kobj_attribute *attr,
			      const char *buf, size_t count)
{
	struct hccs_dev *hdev = device_kobj_to_hccs_dev(kobj);
	bool all_in_idle;
	u8 port_type;
	int ret;

	ret = hccs_parse_pm_port_type(hdev, buf, &port_type);
	if (ret)
		return ret;

	mutex_lock(&hdev->lock);
	ret = hccs_get_all_spec_port_idle_sta(hdev, port_type, &all_in_idle);
	if (ret)
		goto out;
	if (!all_in_idle) {
		ret = -EBUSY;
		dev_err(hdev->dev, "please don't decrese lanes on high load with %s, ret = %d.\n",
			hccs_port_type_to_name(hdev, port_type), ret);
		goto out;
	}

	ret = hccs_start_dec_lane(hdev, port_type);
out:
	mutex_unlock(&hdev->lock);

	return ret == 0 ? count : ret;
}
static struct kobj_attribute dec_lane_of_type_attr =
		__ATTR(dec_lane_of_type, 0200, NULL, dec_lane_of_type_store);

static ssize_t inc_lane_of_type_store(struct kobject *kobj, struct kobj_attribute *attr,
			      const char *buf, size_t count)
{
	struct hccs_dev *hdev = device_kobj_to_hccs_dev(kobj);
	bool full_lane;
	u8 port_type;
	int ret;

	ret = hccs_parse_pm_port_type(hdev, buf, &port_type);
	if (ret)
		return ret;

	mutex_lock(&hdev->lock);
	ret = hccs_get_all_spec_port_full_lane_sta(hdev, port_type, &full_lane);
	if (ret || full_lane)
		goto out;

	ret = hccs_start_inc_lane(hdev, port_type);
out:
	mutex_unlock(&hdev->lock);
	return ret == 0 ? count : ret;
}
static struct kobj_attribute inc_lane_of_type_attr =
		__ATTR(inc_lane_of_type, 0200, NULL, inc_lane_of_type_store);

static ssize_t available_inc_dec_lane_types_show(struct kobject *kobj,
						 struct kobj_attribute *attr,
						 char *buf)
{
	struct hccs_dev *hdev = device_kobj_to_hccs_dev(kobj);

	if (hdev->caps & HCCS_CAPS_HCCS_V2_PM)
		return sysfs_emit(buf, "%s\n",
				  hccs_port_type_to_name(hdev, HCCS_V2));

	return -EINVAL;
}
static struct kobj_attribute available_inc_dec_lane_types_attr =
		__ATTR(available_inc_dec_lane_types, 0444,
		       available_inc_dec_lane_types_show, NULL);

static ssize_t used_types_show(struct kobject *kobj,
			       struct kobj_attribute *attr, char *buf)
{
	struct hccs_dev *hdev = device_kobj_to_hccs_dev(kobj);
	int len = 0;
	u16 i;

	for (i = 0; i < hdev->used_type_num - 1; i++)
		len += sysfs_emit(&buf[len], "%s ", hdev->type_name_maps[i].name);
	len += sysfs_emit(&buf[len], "%s\n", hdev->type_name_maps[i].name);

	return len;
}
static struct kobj_attribute used_types_attr =
		__ATTR(used_types, 0444, used_types_show, NULL);

static void hccs_remove_misc_sysfs(struct hccs_dev *hdev)
{
	sysfs_remove_file(&hdev->dev->kobj, &used_types_attr.attr);

	if (!(hdev->caps & HCCS_CAPS_HCCS_V2_PM))
		return;

	sysfs_remove_file(&hdev->dev->kobj,
			  &available_inc_dec_lane_types_attr.attr);
	sysfs_remove_file(&hdev->dev->kobj, &dec_lane_of_type_attr.attr);
	sysfs_remove_file(&hdev->dev->kobj, &inc_lane_of_type_attr.attr);
}

static int hccs_add_misc_sysfs(struct hccs_dev *hdev)
{
	int ret;

	ret = sysfs_create_file(&hdev->dev->kobj, &used_types_attr.attr);
	if (ret)
		return ret;

	if (!(hdev->caps & HCCS_CAPS_HCCS_V2_PM))
		return 0;

	ret = sysfs_create_file(&hdev->dev->kobj,
				&available_inc_dec_lane_types_attr.attr);
	if (ret)
		goto used_types_remove;

	ret = sysfs_create_file(&hdev->dev->kobj, &dec_lane_of_type_attr.attr);
	if (ret)
		goto inc_dec_lane_types_remove;

	ret = sysfs_create_file(&hdev->dev->kobj, &inc_lane_of_type_attr.attr);
	if (ret)
		goto dec_lane_of_type_remove;

	return 0;

dec_lane_of_type_remove:
	sysfs_remove_file(&hdev->dev->kobj, &dec_lane_of_type_attr.attr);
inc_dec_lane_types_remove:
	sysfs_remove_file(&hdev->dev->kobj,
			  &available_inc_dec_lane_types_attr.attr);
used_types_remove:
	sysfs_remove_file(&hdev->dev->kobj, &used_types_attr.attr);
	return ret;
}

static void hccs_remove_die_dir(struct hccs_die_info *die)
{
	struct hccs_port_info *port;
	u8 i;

	for (i = 0; i < die->port_num; i++) {
		port = &die->ports[i];
		if (port->dir_created)
			kobject_put(&port->kobj);
	}

	kobject_put(&die->kobj);
}

static void hccs_remove_chip_dir(struct hccs_chip_info *chip)
{
	struct hccs_die_info *die;
	u8 i;

	for (i = 0; i < chip->die_num; i++) {
		die = &chip->dies[i];
		if (die->dir_created)
			hccs_remove_die_dir(die);
	}

	kobject_put(&chip->kobj);
}

static void hccs_remove_topo_dirs(struct hccs_dev *hdev)
{
	u8 i;

	for (i = 0; i < hdev->chip_num; i++)
		hccs_remove_chip_dir(&hdev->chips[i]);

	hccs_remove_misc_sysfs(hdev);
}

static int hccs_create_hccs_dir(struct hccs_dev *hdev,
				struct hccs_die_info *die,
				struct hccs_port_info *port)
{
	int ret;

	ret = kobject_init_and_add(&port->kobj, &hccs_port_type,
				   &die->kobj, "hccs%u", port->port_id);
	if (ret) {
		kobject_put(&port->kobj);
		return ret;
	}

	return 0;
}

static int hccs_create_die_dir(struct hccs_dev *hdev,
			       struct hccs_chip_info *chip,
			       struct hccs_die_info *die)
{
	struct hccs_port_info *port;
	int ret;
	u16 i;

	ret = kobject_init_and_add(&die->kobj, &hccs_die_type,
				   &chip->kobj, "die%u", die->die_id);
	if (ret) {
		kobject_put(&die->kobj);
		return ret;
	}

	for (i = 0; i < die->port_num; i++) {
		port = &die->ports[i];
		ret = hccs_create_hccs_dir(hdev, die, port);
		if (ret) {
			dev_err(hdev->dev, "create hccs%u dir failed.\n",
				port->port_id);
			goto err;
		}
		port->dir_created = true;
	}

	return 0;
err:
	hccs_remove_die_dir(die);

	return ret;
}

static int hccs_create_chip_dir(struct hccs_dev *hdev,
				struct hccs_chip_info *chip)
{
	struct hccs_die_info *die;
	int ret;
	u16 id;

	ret = kobject_init_and_add(&chip->kobj, &hccs_chip_type,
				   &hdev->dev->kobj, "chip%u", chip->chip_id);
	if (ret) {
		kobject_put(&chip->kobj);
		return ret;
	}

	for (id = 0; id < chip->die_num; id++) {
		die = &chip->dies[id];
		ret = hccs_create_die_dir(hdev, chip, die);
		if (ret)
			goto err;
		die->dir_created = true;
	}

	return 0;
err:
	hccs_remove_chip_dir(chip);

	return ret;
}

static int hccs_create_topo_dirs(struct hccs_dev *hdev)
{
	struct hccs_chip_info *chip;
	u8 id, k;
	int ret;

	for (id = 0; id < hdev->chip_num; id++) {
		chip = &hdev->chips[id];
		ret = hccs_create_chip_dir(hdev, chip);
		if (ret) {
			dev_err(hdev->dev, "init chip%u dir failed!\n", id);
			goto err;
		}
	}

	ret = hccs_add_misc_sysfs(hdev);
	if (ret) {
		dev_err(hdev->dev, "create misc sysfs interface failed, ret = %d\n", ret);
		goto err;
	}

	return 0;
err:
	for (k = 0; k < id; k++)
		hccs_remove_chip_dir(&hdev->chips[k]);

	return ret;
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

	/*
	 * Here would never be failure as the driver and device has been matched.
	 */
	hdev->verspec_data = acpi_device_get_match_data(hdev->dev);

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

	rc = hccs_init_type_name_maps(hdev);
	if (rc)
		goto unregister_pcc_chan;

	rc = hccs_create_topo_dirs(hdev);
	if (rc)
		goto unregister_pcc_chan;

	return 0;

unregister_pcc_chan:
	hccs_unregister_pcc_channel(hdev);

	return rc;
}

static void hccs_remove(struct platform_device *pdev)
{
	struct hccs_dev *hdev = platform_get_drvdata(pdev);

	hccs_remove_topo_dirs(hdev);
	hccs_unregister_pcc_channel(hdev);
}

static const struct hccs_verspecific_data hisi04b1_verspec_data = {
	.rx_callback = NULL,
	.wait_cmd_complete = hccs_wait_cmd_complete_by_poll,
	.fill_pcc_shared_mem = hccs_fill_pcc_shared_mem_region,
	.shared_mem_size = sizeof(struct acpi_pcct_shared_memory),
	.has_txdone_irq = false,
};

static const struct hccs_verspecific_data hisi04b2_verspec_data = {
	.rx_callback = hccs_pcc_rx_callback,
	.wait_cmd_complete = hccs_wait_cmd_complete_by_irq,
	.fill_pcc_shared_mem = hccs_fill_ext_pcc_shared_mem_region,
	.shared_mem_size = sizeof(struct acpi_pcct_ext_pcc_shared_memory),
	.has_txdone_irq = true,
};

static const struct acpi_device_id hccs_acpi_match[] = {
	{ "HISI04B1", (unsigned long)&hisi04b1_verspec_data},
	{ "HISI04B2", (unsigned long)&hisi04b2_verspec_data},
	{ }
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
