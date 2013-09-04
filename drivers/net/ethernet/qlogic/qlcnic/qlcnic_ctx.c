/*
 * QLogic qlcnic NIC Driver
 * Copyright (c) 2009-2013 QLogic Corporation
 *
 * See LICENSE.qlcnic for copyright and licensing details.
 */

#include "qlcnic.h"

static const struct qlcnic_mailbox_metadata qlcnic_mbx_tbl[] = {
	{QLCNIC_CMD_CREATE_RX_CTX, 4, 1},
	{QLCNIC_CMD_DESTROY_RX_CTX, 2, 1},
	{QLCNIC_CMD_CREATE_TX_CTX, 4, 1},
	{QLCNIC_CMD_DESTROY_TX_CTX, 2, 1},
	{QLCNIC_CMD_INTRPT_TEST, 4, 1},
	{QLCNIC_CMD_SET_MTU, 4, 1},
	{QLCNIC_CMD_READ_PHY, 4, 2},
	{QLCNIC_CMD_WRITE_PHY, 5, 1},
	{QLCNIC_CMD_READ_HW_REG, 4, 1},
	{QLCNIC_CMD_GET_FLOW_CTL, 4, 2},
	{QLCNIC_CMD_SET_FLOW_CTL, 4, 1},
	{QLCNIC_CMD_READ_MAX_MTU, 4, 2},
	{QLCNIC_CMD_READ_MAX_LRO, 4, 2},
	{QLCNIC_CMD_MAC_ADDRESS, 4, 3},
	{QLCNIC_CMD_GET_PCI_INFO, 4, 1},
	{QLCNIC_CMD_GET_NIC_INFO, 4, 1},
	{QLCNIC_CMD_SET_NIC_INFO, 4, 1},
	{QLCNIC_CMD_GET_ESWITCH_CAPABILITY, 4, 3},
	{QLCNIC_CMD_TOGGLE_ESWITCH, 4, 1},
	{QLCNIC_CMD_GET_ESWITCH_STATUS, 4, 3},
	{QLCNIC_CMD_SET_PORTMIRRORING, 4, 1},
	{QLCNIC_CMD_CONFIGURE_ESWITCH, 4, 1},
	{QLCNIC_CMD_GET_MAC_STATS, 4, 1},
	{QLCNIC_CMD_GET_ESWITCH_PORT_CONFIG, 4, 3},
	{QLCNIC_CMD_GET_ESWITCH_STATS, 5, 1},
	{QLCNIC_CMD_CONFIG_PORT, 4, 1},
	{QLCNIC_CMD_TEMP_SIZE, 4, 4},
	{QLCNIC_CMD_GET_TEMP_HDR, 4, 1},
	{QLCNIC_CMD_82XX_SET_DRV_VER, 4, 1},
	{QLCNIC_CMD_GET_LED_STATUS, 4, 2},
};

static inline u32 qlcnic_get_cmd_signature(struct qlcnic_hardware_context *ahw)
{
	return (ahw->pci_func & 0xff) | ((ahw->fw_hal_version & 0xff) << 8) |
	       (0xcafe << 16);
}

/* Allocate mailbox registers */
int qlcnic_82xx_alloc_mbx_args(struct qlcnic_cmd_args *mbx,
			       struct qlcnic_adapter *adapter, u32 type)
{
	int i, size;
	const struct qlcnic_mailbox_metadata *mbx_tbl;

	mbx_tbl = qlcnic_mbx_tbl;
	size = ARRAY_SIZE(qlcnic_mbx_tbl);
	for (i = 0; i < size; i++) {
		if (type == mbx_tbl[i].cmd) {
			mbx->req.num = mbx_tbl[i].in_args;
			mbx->rsp.num = mbx_tbl[i].out_args;
			mbx->req.arg = kcalloc(mbx->req.num,
					       sizeof(u32), GFP_ATOMIC);
			if (!mbx->req.arg)
				return -ENOMEM;
			mbx->rsp.arg = kcalloc(mbx->rsp.num,
					       sizeof(u32), GFP_ATOMIC);
			if (!mbx->rsp.arg) {
				kfree(mbx->req.arg);
				mbx->req.arg = NULL;
				return -ENOMEM;
			}
			memset(mbx->req.arg, 0, sizeof(u32) * mbx->req.num);
			memset(mbx->rsp.arg, 0, sizeof(u32) * mbx->rsp.num);
			mbx->req.arg[0] = type;
			break;
		}
	}
	return 0;
}

/* Free up mailbox registers */
void qlcnic_free_mbx_args(struct qlcnic_cmd_args *cmd)
{
	kfree(cmd->req.arg);
	cmd->req.arg = NULL;
	kfree(cmd->rsp.arg);
	cmd->rsp.arg = NULL;
}

static int qlcnic_is_valid_nic_func(struct qlcnic_adapter *adapter, u8 pci_func)
{
	int i;

	for (i = 0; i < adapter->ahw->act_pci_func; i++) {
		if (adapter->npars[i].pci_func == pci_func)
			return i;
	}

	return -1;
}

static u32
qlcnic_poll_rsp(struct qlcnic_adapter *adapter)
{
	u32 rsp;
	int timeout = 0, err = 0;

	do {
		/* give atleast 1ms for firmware to respond */
		mdelay(1);

		if (++timeout > QLCNIC_OS_CRB_RETRY_COUNT)
			return QLCNIC_CDRP_RSP_TIMEOUT;

		rsp = QLCRD32(adapter, QLCNIC_CDRP_CRB_OFFSET, &err);
	} while (!QLCNIC_CDRP_IS_RSP(rsp));

	return rsp;
}

int qlcnic_82xx_issue_cmd(struct qlcnic_adapter *adapter,
			  struct qlcnic_cmd_args *cmd)
{
	int i, err = 0;
	u32 rsp;
	u32 signature;
	struct pci_dev *pdev = adapter->pdev;
	struct qlcnic_hardware_context *ahw = adapter->ahw;
	const char *fmt;

	signature = qlcnic_get_cmd_signature(ahw);

	/* Acquire semaphore before accessing CRB */
	if (qlcnic_api_lock(adapter)) {
		cmd->rsp.arg[0] = QLCNIC_RCODE_TIMEOUT;
		return cmd->rsp.arg[0];
	}

	QLCWR32(adapter, QLCNIC_SIGN_CRB_OFFSET, signature);
	for (i = 1; i < QLCNIC_CDRP_MAX_ARGS; i++)
		QLCWR32(adapter, QLCNIC_CDRP_ARG(i), cmd->req.arg[i]);
	QLCWR32(adapter, QLCNIC_CDRP_CRB_OFFSET,
		QLCNIC_CDRP_FORM_CMD(cmd->req.arg[0]));
	rsp = qlcnic_poll_rsp(adapter);

	if (rsp == QLCNIC_CDRP_RSP_TIMEOUT) {
		dev_err(&pdev->dev, "card response timeout.\n");
		cmd->rsp.arg[0] = QLCNIC_RCODE_TIMEOUT;
	} else if (rsp == QLCNIC_CDRP_RSP_FAIL) {
		cmd->rsp.arg[0] = QLCRD32(adapter, QLCNIC_CDRP_ARG(1), &err);
		switch (cmd->rsp.arg[0]) {
		case QLCNIC_RCODE_INVALID_ARGS:
			fmt = "CDRP invalid args: [%d]\n";
			break;
		case QLCNIC_RCODE_NOT_SUPPORTED:
		case QLCNIC_RCODE_NOT_IMPL:
			fmt = "CDRP command not supported: [%d]\n";
			break;
		case QLCNIC_RCODE_NOT_PERMITTED:
			fmt = "CDRP requested action not permitted: [%d]\n";
			break;
		case QLCNIC_RCODE_INVALID:
			fmt = "CDRP invalid or unknown cmd received: [%d]\n";
			break;
		case QLCNIC_RCODE_TIMEOUT:
			fmt = "CDRP command timeout: [%d]\n";
			break;
		default:
			fmt = "CDRP command failed: [%d]\n";
			break;
		}
		dev_err(&pdev->dev, fmt, cmd->rsp.arg[0]);
	} else if (rsp == QLCNIC_CDRP_RSP_OK)
		cmd->rsp.arg[0] = QLCNIC_RCODE_SUCCESS;

	for (i = 1; i < cmd->rsp.num; i++)
		cmd->rsp.arg[i] = QLCRD32(adapter, QLCNIC_CDRP_ARG(i), &err);

	/* Release semaphore */
	qlcnic_api_unlock(adapter);
	return cmd->rsp.arg[0];
}

int qlcnic_fw_cmd_set_drv_version(struct qlcnic_adapter *adapter, u32 fw_cmd)
{
	struct qlcnic_cmd_args cmd;
	u32 arg1, arg2, arg3;
	char drv_string[12];
	int err = 0;

	memset(drv_string, 0, sizeof(drv_string));
	snprintf(drv_string, sizeof(drv_string), "%d"".""%d"".""%d",
		 _QLCNIC_LINUX_MAJOR, _QLCNIC_LINUX_MINOR,
		 _QLCNIC_LINUX_SUBVERSION);

	err = qlcnic_alloc_mbx_args(&cmd, adapter, fw_cmd);
	if (err)
		return err;

	memcpy(&arg1, drv_string, sizeof(u32));
	memcpy(&arg2, drv_string + 4, sizeof(u32));
	memcpy(&arg3, drv_string + 8, sizeof(u32));

	cmd.req.arg[1] = arg1;
	cmd.req.arg[2] = arg2;
	cmd.req.arg[3] = arg3;

	err = qlcnic_issue_cmd(adapter, &cmd);
	if (err) {
		dev_info(&adapter->pdev->dev,
			 "Failed to set driver version in firmware\n");
		err = -EIO;
	}
	qlcnic_free_mbx_args(&cmd);
	return err;
}

int
qlcnic_fw_cmd_set_mtu(struct qlcnic_adapter *adapter, int mtu)
{
	int err = 0;
	struct qlcnic_cmd_args cmd;
	struct qlcnic_recv_context *recv_ctx = adapter->recv_ctx;

	if (recv_ctx->state != QLCNIC_HOST_CTX_STATE_ACTIVE)
		return err;
	err = qlcnic_alloc_mbx_args(&cmd, adapter, QLCNIC_CMD_SET_MTU);
	if (err)
		return err;

	cmd.req.arg[1] = recv_ctx->context_id;
	cmd.req.arg[2] = mtu;

	err = qlcnic_issue_cmd(adapter, &cmd);
	if (err) {
		dev_err(&adapter->pdev->dev, "Failed to set mtu\n");
		err = -EIO;
	}
	qlcnic_free_mbx_args(&cmd);
	return err;
}

int qlcnic_82xx_fw_cmd_create_rx_ctx(struct qlcnic_adapter *adapter)
{
	void *addr;
	struct qlcnic_hostrq_rx_ctx *prq;
	struct qlcnic_cardrsp_rx_ctx *prsp;
	struct qlcnic_hostrq_rds_ring *prq_rds;
	struct qlcnic_hostrq_sds_ring *prq_sds;
	struct qlcnic_cardrsp_rds_ring *prsp_rds;
	struct qlcnic_cardrsp_sds_ring *prsp_sds;
	struct qlcnic_host_rds_ring *rds_ring;
	struct qlcnic_host_sds_ring *sds_ring;
	struct qlcnic_cmd_args cmd;

	dma_addr_t hostrq_phys_addr, cardrsp_phys_addr;
	u64 phys_addr;

	u8 i, nrds_rings, nsds_rings;
	u16 temp_u16;
	size_t rq_size, rsp_size;
	u32 cap, reg, val, reg2;
	int err;

	struct qlcnic_recv_context *recv_ctx = adapter->recv_ctx;

	nrds_rings = adapter->max_rds_rings;
	nsds_rings = adapter->max_sds_rings;

	rq_size =
		SIZEOF_HOSTRQ_RX(struct qlcnic_hostrq_rx_ctx, nrds_rings,
						nsds_rings);
	rsp_size =
		SIZEOF_CARDRSP_RX(struct qlcnic_cardrsp_rx_ctx, nrds_rings,
						nsds_rings);

	addr = dma_alloc_coherent(&adapter->pdev->dev, rq_size,
			&hostrq_phys_addr, GFP_KERNEL);
	if (addr == NULL)
		return -ENOMEM;
	prq = addr;

	addr = dma_alloc_coherent(&adapter->pdev->dev, rsp_size,
			&cardrsp_phys_addr, GFP_KERNEL);
	if (addr == NULL) {
		err = -ENOMEM;
		goto out_free_rq;
	}
	prsp = addr;

	prq->host_rsp_dma_addr = cpu_to_le64(cardrsp_phys_addr);

	cap = (QLCNIC_CAP0_LEGACY_CONTEXT | QLCNIC_CAP0_LEGACY_MN
						| QLCNIC_CAP0_VALIDOFF);
	cap |= (QLCNIC_CAP0_JUMBO_CONTIGUOUS | QLCNIC_CAP0_LRO_CONTIGUOUS);

	temp_u16 = offsetof(struct qlcnic_hostrq_rx_ctx, msix_handler);
	prq->valid_field_offset = cpu_to_le16(temp_u16);
	prq->txrx_sds_binding = nsds_rings - 1;

	prq->capabilities[0] = cpu_to_le32(cap);
	prq->host_int_crb_mode =
		cpu_to_le32(QLCNIC_HOST_INT_CRB_MODE_SHARED);
	prq->host_rds_crb_mode =
		cpu_to_le32(QLCNIC_HOST_RDS_CRB_MODE_UNIQUE);

	prq->num_rds_rings = cpu_to_le16(nrds_rings);
	prq->num_sds_rings = cpu_to_le16(nsds_rings);
	prq->rds_ring_offset = 0;

	val = le32_to_cpu(prq->rds_ring_offset) +
		(sizeof(struct qlcnic_hostrq_rds_ring) * nrds_rings);
	prq->sds_ring_offset = cpu_to_le32(val);

	prq_rds = (struct qlcnic_hostrq_rds_ring *)(prq->data +
			le32_to_cpu(prq->rds_ring_offset));

	for (i = 0; i < nrds_rings; i++) {

		rds_ring = &recv_ctx->rds_rings[i];
		rds_ring->producer = 0;

		prq_rds[i].host_phys_addr = cpu_to_le64(rds_ring->phys_addr);
		prq_rds[i].ring_size = cpu_to_le32(rds_ring->num_desc);
		prq_rds[i].ring_kind = cpu_to_le32(i);
		prq_rds[i].buff_size = cpu_to_le64(rds_ring->dma_size);
	}

	prq_sds = (struct qlcnic_hostrq_sds_ring *)(prq->data +
			le32_to_cpu(prq->sds_ring_offset));

	for (i = 0; i < nsds_rings; i++) {

		sds_ring = &recv_ctx->sds_rings[i];
		sds_ring->consumer = 0;
		memset(sds_ring->desc_head, 0, STATUS_DESC_RINGSIZE(sds_ring));

		prq_sds[i].host_phys_addr = cpu_to_le64(sds_ring->phys_addr);
		prq_sds[i].ring_size = cpu_to_le32(sds_ring->num_desc);
		prq_sds[i].msi_index = cpu_to_le16(i);
	}

	phys_addr = hostrq_phys_addr;
	err = qlcnic_alloc_mbx_args(&cmd, adapter, QLCNIC_CMD_CREATE_RX_CTX);
	if (err)
		goto out_free_rsp;

	cmd.req.arg[1] = MSD(phys_addr);
	cmd.req.arg[2] = LSD(phys_addr);
	cmd.req.arg[3] = rq_size;
	err = qlcnic_issue_cmd(adapter, &cmd);
	if (err) {
		dev_err(&adapter->pdev->dev,
			"Failed to create rx ctx in firmware%d\n", err);
		goto out_free_rsp;
	}

	prsp_rds = ((struct qlcnic_cardrsp_rds_ring *)
			 &prsp->data[le32_to_cpu(prsp->rds_ring_offset)]);

	for (i = 0; i < le16_to_cpu(prsp->num_rds_rings); i++) {
		rds_ring = &recv_ctx->rds_rings[i];

		reg = le32_to_cpu(prsp_rds[i].host_producer_crb);
		rds_ring->crb_rcv_producer = adapter->ahw->pci_base0 + reg;
	}

	prsp_sds = ((struct qlcnic_cardrsp_sds_ring *)
			&prsp->data[le32_to_cpu(prsp->sds_ring_offset)]);

	for (i = 0; i < le16_to_cpu(prsp->num_sds_rings); i++) {
		sds_ring = &recv_ctx->sds_rings[i];

		reg = le32_to_cpu(prsp_sds[i].host_consumer_crb);
		reg2 = le32_to_cpu(prsp_sds[i].interrupt_crb);

		sds_ring->crb_sts_consumer = adapter->ahw->pci_base0 + reg;
		sds_ring->crb_intr_mask = adapter->ahw->pci_base0 + reg2;
	}

	recv_ctx->state = le32_to_cpu(prsp->host_ctx_state);
	recv_ctx->context_id = le16_to_cpu(prsp->context_id);
	recv_ctx->virt_port = prsp->virt_port;

	qlcnic_free_mbx_args(&cmd);
out_free_rsp:
	dma_free_coherent(&adapter->pdev->dev, rsp_size, prsp,
			  cardrsp_phys_addr);
out_free_rq:
	dma_free_coherent(&adapter->pdev->dev, rq_size, prq, hostrq_phys_addr);
	return err;
}

void qlcnic_82xx_fw_cmd_del_rx_ctx(struct qlcnic_adapter *adapter)
{
	int err;
	struct qlcnic_cmd_args cmd;
	struct qlcnic_recv_context *recv_ctx = adapter->recv_ctx;

	err = qlcnic_alloc_mbx_args(&cmd, adapter, QLCNIC_CMD_DESTROY_RX_CTX);
	if (err)
		return;

	cmd.req.arg[1] = recv_ctx->context_id;
	err = qlcnic_issue_cmd(adapter, &cmd);
	if (err)
		dev_err(&adapter->pdev->dev,
			"Failed to destroy rx ctx in firmware\n");

	recv_ctx->state = QLCNIC_HOST_CTX_STATE_FREED;
	qlcnic_free_mbx_args(&cmd);
}

int qlcnic_82xx_fw_cmd_create_tx_ctx(struct qlcnic_adapter *adapter,
				     struct qlcnic_host_tx_ring *tx_ring,
				     int ring)
{
	struct qlcnic_hostrq_tx_ctx	*prq;
	struct qlcnic_hostrq_cds_ring	*prq_cds;
	struct qlcnic_cardrsp_tx_ctx	*prsp;
	void	*rq_addr, *rsp_addr;
	size_t	rq_size, rsp_size;
	u32	temp;
	struct qlcnic_cmd_args cmd;
	int	err;
	u64	phys_addr;
	dma_addr_t	rq_phys_addr, rsp_phys_addr;

	/* reset host resources */
	tx_ring->producer = 0;
	tx_ring->sw_consumer = 0;
	*(tx_ring->hw_consumer) = 0;

	rq_size = SIZEOF_HOSTRQ_TX(struct qlcnic_hostrq_tx_ctx);
	rq_addr = dma_alloc_coherent(&adapter->pdev->dev, rq_size,
				     &rq_phys_addr, GFP_KERNEL | __GFP_ZERO);
	if (!rq_addr)
		return -ENOMEM;

	rsp_size = SIZEOF_CARDRSP_TX(struct qlcnic_cardrsp_tx_ctx);
	rsp_addr = dma_alloc_coherent(&adapter->pdev->dev, rsp_size,
				      &rsp_phys_addr, GFP_KERNEL | __GFP_ZERO);
	if (!rsp_addr) {
		err = -ENOMEM;
		goto out_free_rq;
	}

	prq = rq_addr;

	prsp = rsp_addr;

	prq->host_rsp_dma_addr = cpu_to_le64(rsp_phys_addr);

	temp = (QLCNIC_CAP0_LEGACY_CONTEXT | QLCNIC_CAP0_LEGACY_MN |
					QLCNIC_CAP0_LSO);
	prq->capabilities[0] = cpu_to_le32(temp);

	prq->host_int_crb_mode =
		cpu_to_le32(QLCNIC_HOST_INT_CRB_MODE_SHARED);
	prq->msi_index = 0;

	prq->interrupt_ctl = 0;
	prq->cmd_cons_dma_addr = cpu_to_le64(tx_ring->hw_cons_phys_addr);

	prq_cds = &prq->cds_ring;

	prq_cds->host_phys_addr = cpu_to_le64(tx_ring->phys_addr);
	prq_cds->ring_size = cpu_to_le32(tx_ring->num_desc);

	phys_addr = rq_phys_addr;

	err = qlcnic_alloc_mbx_args(&cmd, adapter, QLCNIC_CMD_CREATE_TX_CTX);
	if (err)
		goto out_free_rsp;

	cmd.req.arg[1] = MSD(phys_addr);
	cmd.req.arg[2] = LSD(phys_addr);
	cmd.req.arg[3] = rq_size;
	err = qlcnic_issue_cmd(adapter, &cmd);

	if (err == QLCNIC_RCODE_SUCCESS) {
		temp = le32_to_cpu(prsp->cds_ring.host_producer_crb);
		tx_ring->crb_cmd_producer = adapter->ahw->pci_base0 + temp;
		tx_ring->ctx_id = le16_to_cpu(prsp->context_id);
	} else {
		dev_err(&adapter->pdev->dev,
			"Failed to create tx ctx in firmware%d\n", err);
		err = -EIO;
	}

	qlcnic_free_mbx_args(&cmd);

out_free_rsp:
	dma_free_coherent(&adapter->pdev->dev, rsp_size, rsp_addr,
			  rsp_phys_addr);
out_free_rq:
	dma_free_coherent(&adapter->pdev->dev, rq_size, rq_addr, rq_phys_addr);

	return err;
}

void qlcnic_82xx_fw_cmd_del_tx_ctx(struct qlcnic_adapter *adapter,
				   struct qlcnic_host_tx_ring *tx_ring)
{
	struct qlcnic_cmd_args cmd;
	int ret;

	ret = qlcnic_alloc_mbx_args(&cmd, adapter, QLCNIC_CMD_DESTROY_TX_CTX);
	if (ret)
		return;

	cmd.req.arg[1] = tx_ring->ctx_id;
	if (qlcnic_issue_cmd(adapter, &cmd))
		dev_err(&adapter->pdev->dev,
			"Failed to destroy tx ctx in firmware\n");
	qlcnic_free_mbx_args(&cmd);
}

int
qlcnic_fw_cmd_set_port(struct qlcnic_adapter *adapter, u32 config)
{
	int err;
	struct qlcnic_cmd_args cmd;

	err = qlcnic_alloc_mbx_args(&cmd, adapter, QLCNIC_CMD_CONFIG_PORT);
	if (err)
		return err;

	cmd.req.arg[1] = config;
	err = qlcnic_issue_cmd(adapter, &cmd);
	qlcnic_free_mbx_args(&cmd);
	return err;
}

int qlcnic_alloc_hw_resources(struct qlcnic_adapter *adapter)
{
	void *addr;
	int err, ring;
	struct qlcnic_recv_context *recv_ctx;
	struct qlcnic_host_rds_ring *rds_ring;
	struct qlcnic_host_sds_ring *sds_ring;
	struct qlcnic_host_tx_ring *tx_ring;
	__le32 *ptr;

	struct pci_dev *pdev = adapter->pdev;

	recv_ctx = adapter->recv_ctx;

	for (ring = 0; ring < adapter->max_drv_tx_rings; ring++) {
		tx_ring = &adapter->tx_ring[ring];
		ptr = (__le32 *)dma_alloc_coherent(&pdev->dev, sizeof(u32),
						   &tx_ring->hw_cons_phys_addr,
						   GFP_KERNEL);
		if (ptr == NULL)
			return -ENOMEM;

		tx_ring->hw_consumer = ptr;
		/* cmd desc ring */
		addr = dma_alloc_coherent(&pdev->dev, TX_DESC_RINGSIZE(tx_ring),
					  &tx_ring->phys_addr,
					  GFP_KERNEL);
		if (addr == NULL) {
			err = -ENOMEM;
			goto err_out_free;
		}

		tx_ring->desc_head = addr;
	}

	for (ring = 0; ring < adapter->max_rds_rings; ring++) {
		rds_ring = &recv_ctx->rds_rings[ring];
		addr = dma_alloc_coherent(&adapter->pdev->dev,
					  RCV_DESC_RINGSIZE(rds_ring),
					  &rds_ring->phys_addr, GFP_KERNEL);
		if (addr == NULL) {
			err = -ENOMEM;
			goto err_out_free;
		}
		rds_ring->desc_head = addr;

	}

	for (ring = 0; ring < adapter->max_sds_rings; ring++) {
		sds_ring = &recv_ctx->sds_rings[ring];

		addr = dma_alloc_coherent(&adapter->pdev->dev,
					  STATUS_DESC_RINGSIZE(sds_ring),
					  &sds_ring->phys_addr, GFP_KERNEL);
		if (addr == NULL) {
			err = -ENOMEM;
			goto err_out_free;
		}
		sds_ring->desc_head = addr;
	}

	return 0;

err_out_free:
	qlcnic_free_hw_resources(adapter);
	return err;
}

int qlcnic_fw_create_ctx(struct qlcnic_adapter *dev)
{
	int i, err, ring;

	if (dev->flags & QLCNIC_NEED_FLR) {
		pci_reset_function(dev->pdev);
		dev->flags &= ~QLCNIC_NEED_FLR;
	}

	if (qlcnic_83xx_check(dev) && (dev->flags & QLCNIC_MSIX_ENABLED)) {
		if (dev->ahw->diag_test != QLCNIC_LOOPBACK_TEST) {
			err = qlcnic_83xx_config_intrpt(dev, 1);
			if (err)
				return err;
		}
	}

	err = qlcnic_fw_cmd_create_rx_ctx(dev);
	if (err)
		goto err_out;

	for (ring = 0; ring < dev->max_drv_tx_rings; ring++) {
		err = qlcnic_fw_cmd_create_tx_ctx(dev,
						  &dev->tx_ring[ring],
						  ring);
		if (err) {
			qlcnic_fw_cmd_del_rx_ctx(dev);
			if (ring == 0)
				goto err_out;

			for (i = 0; i < ring; i++)
				qlcnic_fw_cmd_del_tx_ctx(dev, &dev->tx_ring[i]);

			goto err_out;
		}
	}

	set_bit(__QLCNIC_FW_ATTACHED, &dev->state);
	return 0;

err_out:
	if (qlcnic_83xx_check(dev) && (dev->flags & QLCNIC_MSIX_ENABLED)) {
		if (dev->ahw->diag_test != QLCNIC_LOOPBACK_TEST)
			qlcnic_83xx_config_intrpt(dev, 0);
	}
	return err;
}

void qlcnic_fw_destroy_ctx(struct qlcnic_adapter *adapter)
{
	int ring;

	if (test_and_clear_bit(__QLCNIC_FW_ATTACHED, &adapter->state)) {
		qlcnic_fw_cmd_del_rx_ctx(adapter);
		for (ring = 0; ring < adapter->max_drv_tx_rings; ring++)
			qlcnic_fw_cmd_del_tx_ctx(adapter,
						 &adapter->tx_ring[ring]);

		if (qlcnic_83xx_check(adapter) &&
		    (adapter->flags & QLCNIC_MSIX_ENABLED)) {
			if (adapter->ahw->diag_test != QLCNIC_LOOPBACK_TEST)
				qlcnic_83xx_config_intrpt(adapter, 0);
		}
		/* Allow dma queues to drain after context reset */
		mdelay(20);
	}
}

void qlcnic_free_hw_resources(struct qlcnic_adapter *adapter)
{
	struct qlcnic_recv_context *recv_ctx;
	struct qlcnic_host_rds_ring *rds_ring;
	struct qlcnic_host_sds_ring *sds_ring;
	struct qlcnic_host_tx_ring *tx_ring;
	int ring;

	recv_ctx = adapter->recv_ctx;

	for (ring = 0; ring < adapter->max_drv_tx_rings; ring++) {
		tx_ring = &adapter->tx_ring[ring];
		if (tx_ring->hw_consumer != NULL) {
			dma_free_coherent(&adapter->pdev->dev, sizeof(u32),
					  tx_ring->hw_consumer,
					  tx_ring->hw_cons_phys_addr);

			tx_ring->hw_consumer = NULL;
		}

		if (tx_ring->desc_head != NULL) {
			dma_free_coherent(&adapter->pdev->dev,
					  TX_DESC_RINGSIZE(tx_ring),
					  tx_ring->desc_head,
					  tx_ring->phys_addr);
			tx_ring->desc_head = NULL;
		}
	}

	for (ring = 0; ring < adapter->max_rds_rings; ring++) {
		rds_ring = &recv_ctx->rds_rings[ring];

		if (rds_ring->desc_head != NULL) {
			dma_free_coherent(&adapter->pdev->dev,
					RCV_DESC_RINGSIZE(rds_ring),
					rds_ring->desc_head,
					rds_ring->phys_addr);
			rds_ring->desc_head = NULL;
		}
	}

	for (ring = 0; ring < adapter->max_sds_rings; ring++) {
		sds_ring = &recv_ctx->sds_rings[ring];

		if (sds_ring->desc_head != NULL) {
			dma_free_coherent(&adapter->pdev->dev,
				STATUS_DESC_RINGSIZE(sds_ring),
				sds_ring->desc_head,
				sds_ring->phys_addr);
			sds_ring->desc_head = NULL;
		}
	}
}


int qlcnic_82xx_get_mac_address(struct qlcnic_adapter *adapter, u8 *mac)
{
	int err, i;
	struct qlcnic_cmd_args cmd;
	u32 mac_low, mac_high;

	err = qlcnic_alloc_mbx_args(&cmd, adapter, QLCNIC_CMD_MAC_ADDRESS);
	if (err)
		return err;

	cmd.req.arg[1] = adapter->ahw->pci_func | BIT_8;
	err = qlcnic_issue_cmd(adapter, &cmd);

	if (err == QLCNIC_RCODE_SUCCESS) {
		mac_low = cmd.rsp.arg[1];
		mac_high = cmd.rsp.arg[2];

		for (i = 0; i < 2; i++)
			mac[i] = (u8) (mac_high >> ((1 - i) * 8));
		for (i = 2; i < 6; i++)
			mac[i] = (u8) (mac_low >> ((5 - i) * 8));
	} else {
		dev_err(&adapter->pdev->dev,
			"Failed to get mac address%d\n", err);
		err = -EIO;
	}
	qlcnic_free_mbx_args(&cmd);
	return err;
}

/* Get info of a NIC partition */
int qlcnic_82xx_get_nic_info(struct qlcnic_adapter *adapter,
			     struct qlcnic_info *npar_info, u8 func_id)
{
	int	err;
	dma_addr_t nic_dma_t;
	const struct qlcnic_info_le *nic_info;
	void *nic_info_addr;
	struct qlcnic_cmd_args cmd;
	size_t  nic_size = sizeof(struct qlcnic_info_le);

	nic_info_addr = dma_alloc_coherent(&adapter->pdev->dev, nic_size,
					   &nic_dma_t, GFP_KERNEL | __GFP_ZERO);
	if (!nic_info_addr)
		return -ENOMEM;

	nic_info = nic_info_addr;

	err = qlcnic_alloc_mbx_args(&cmd, adapter, QLCNIC_CMD_GET_NIC_INFO);
	if (err)
		goto out_free_dma;

	cmd.req.arg[1] = MSD(nic_dma_t);
	cmd.req.arg[2] = LSD(nic_dma_t);
	cmd.req.arg[3] = (func_id << 16 | nic_size);
	err = qlcnic_issue_cmd(adapter, &cmd);
	if (err != QLCNIC_RCODE_SUCCESS) {
		dev_err(&adapter->pdev->dev,
			"Failed to get nic info%d\n", err);
		err = -EIO;
	} else {
		npar_info->pci_func = le16_to_cpu(nic_info->pci_func);
		npar_info->op_mode = le16_to_cpu(nic_info->op_mode);
		npar_info->min_tx_bw = le16_to_cpu(nic_info->min_tx_bw);
		npar_info->max_tx_bw = le16_to_cpu(nic_info->max_tx_bw);
		npar_info->phys_port = le16_to_cpu(nic_info->phys_port);
		npar_info->switch_mode = le16_to_cpu(nic_info->switch_mode);
		npar_info->max_tx_ques = le16_to_cpu(nic_info->max_tx_ques);
		npar_info->max_rx_ques = le16_to_cpu(nic_info->max_rx_ques);
		npar_info->capabilities = le32_to_cpu(nic_info->capabilities);
		npar_info->max_mtu = le16_to_cpu(nic_info->max_mtu);
	}

	qlcnic_free_mbx_args(&cmd);
out_free_dma:
	dma_free_coherent(&adapter->pdev->dev, nic_size, nic_info_addr,
			  nic_dma_t);

	return err;
}

/* Configure a NIC partition */
int qlcnic_82xx_set_nic_info(struct qlcnic_adapter *adapter,
			     struct qlcnic_info *nic)
{
	int err = -EIO;
	dma_addr_t nic_dma_t;
	void *nic_info_addr;
	struct qlcnic_cmd_args cmd;
	struct qlcnic_info_le *nic_info;
	size_t nic_size = sizeof(struct qlcnic_info_le);

	if (adapter->ahw->op_mode != QLCNIC_MGMT_FUNC)
		return err;

	nic_info_addr = dma_alloc_coherent(&adapter->pdev->dev, nic_size,
					   &nic_dma_t, GFP_KERNEL | __GFP_ZERO);
	if (!nic_info_addr)
		return -ENOMEM;

	nic_info = nic_info_addr;

	nic_info->pci_func = cpu_to_le16(nic->pci_func);
	nic_info->op_mode = cpu_to_le16(nic->op_mode);
	nic_info->phys_port = cpu_to_le16(nic->phys_port);
	nic_info->switch_mode = cpu_to_le16(nic->switch_mode);
	nic_info->capabilities = cpu_to_le32(nic->capabilities);
	nic_info->max_mac_filters = nic->max_mac_filters;
	nic_info->max_tx_ques = cpu_to_le16(nic->max_tx_ques);
	nic_info->max_rx_ques = cpu_to_le16(nic->max_rx_ques);
	nic_info->min_tx_bw = cpu_to_le16(nic->min_tx_bw);
	nic_info->max_tx_bw = cpu_to_le16(nic->max_tx_bw);

	err = qlcnic_alloc_mbx_args(&cmd, adapter, QLCNIC_CMD_SET_NIC_INFO);
	if (err)
		goto out_free_dma;

	cmd.req.arg[1] = MSD(nic_dma_t);
	cmd.req.arg[2] = LSD(nic_dma_t);
	cmd.req.arg[3] = ((nic->pci_func << 16) | nic_size);
	err = qlcnic_issue_cmd(adapter, &cmd);

	if (err != QLCNIC_RCODE_SUCCESS) {
		dev_err(&adapter->pdev->dev,
			"Failed to set nic info%d\n", err);
		err = -EIO;
	}

	qlcnic_free_mbx_args(&cmd);
out_free_dma:
	dma_free_coherent(&adapter->pdev->dev, nic_size, nic_info_addr,
			  nic_dma_t);

	return err;
}

/* Get PCI Info of a partition */
int qlcnic_82xx_get_pci_info(struct qlcnic_adapter *adapter,
			     struct qlcnic_pci_info *pci_info)
{
	int err = 0, i;
	struct qlcnic_cmd_args cmd;
	dma_addr_t pci_info_dma_t;
	struct qlcnic_pci_info_le *npar;
	void *pci_info_addr;
	size_t npar_size = sizeof(struct qlcnic_pci_info_le);
	size_t pci_size = npar_size * QLCNIC_MAX_PCI_FUNC;

	pci_info_addr = dma_alloc_coherent(&adapter->pdev->dev, pci_size,
					   &pci_info_dma_t,
					   GFP_KERNEL | __GFP_ZERO);
	if (!pci_info_addr)
		return -ENOMEM;

	npar = pci_info_addr;
	err = qlcnic_alloc_mbx_args(&cmd, adapter, QLCNIC_CMD_GET_PCI_INFO);
	if (err)
		goto out_free_dma;

	cmd.req.arg[1] = MSD(pci_info_dma_t);
	cmd.req.arg[2] = LSD(pci_info_dma_t);
	cmd.req.arg[3] = pci_size;
	err = qlcnic_issue_cmd(adapter, &cmd);

	adapter->ahw->act_pci_func = 0;
	if (err == QLCNIC_RCODE_SUCCESS) {
		for (i = 0; i < QLCNIC_MAX_PCI_FUNC; i++, npar++, pci_info++) {
			pci_info->id = le16_to_cpu(npar->id);
			pci_info->active = le16_to_cpu(npar->active);
			pci_info->type = le16_to_cpu(npar->type);
			if (pci_info->type == QLCNIC_TYPE_NIC)
				adapter->ahw->act_pci_func++;
			pci_info->default_port =
				le16_to_cpu(npar->default_port);
			pci_info->tx_min_bw =
				le16_to_cpu(npar->tx_min_bw);
			pci_info->tx_max_bw =
				le16_to_cpu(npar->tx_max_bw);
			memcpy(pci_info->mac, npar->mac, ETH_ALEN);
		}
	} else {
		dev_err(&adapter->pdev->dev,
			"Failed to get PCI Info%d\n", err);
		err = -EIO;
	}

	qlcnic_free_mbx_args(&cmd);
out_free_dma:
	dma_free_coherent(&adapter->pdev->dev, pci_size, pci_info_addr,
		pci_info_dma_t);

	return err;
}

/* Configure eSwitch for port mirroring */
int qlcnic_config_port_mirroring(struct qlcnic_adapter *adapter, u8 id,
				 u8 enable_mirroring, u8 pci_func)
{
	struct device *dev = &adapter->pdev->dev;
	struct qlcnic_cmd_args cmd;
	int err = -EIO;
	u32 arg1;

	if (adapter->ahw->op_mode != QLCNIC_MGMT_FUNC ||
	    !(adapter->eswitch[id].flags & QLCNIC_SWITCH_ENABLE))
		return err;

	arg1 = id | (enable_mirroring ? BIT_4 : 0);
	arg1 |= pci_func << 8;

	err = qlcnic_alloc_mbx_args(&cmd, adapter,
				    QLCNIC_CMD_SET_PORTMIRRORING);
	if (err)
		return err;

	cmd.req.arg[1] = arg1;
	err = qlcnic_issue_cmd(adapter, &cmd);

	if (err != QLCNIC_RCODE_SUCCESS)
		dev_err(dev, "Failed to configure port mirroring for vNIC function %d on eSwitch %d\n",
			pci_func, id);
	else
		dev_info(dev, "Configured port mirroring for vNIC function %d on eSwitch %d\n",
			 pci_func, id);
	qlcnic_free_mbx_args(&cmd);

	return err;
}

int qlcnic_get_port_stats(struct qlcnic_adapter *adapter, const u8 func,
		const u8 rx_tx, struct __qlcnic_esw_statistics *esw_stats) {

	size_t stats_size = sizeof(struct qlcnic_esw_stats_le);
	struct qlcnic_esw_stats_le *stats;
	dma_addr_t stats_dma_t;
	void *stats_addr;
	u32 arg1;
	struct qlcnic_cmd_args cmd;
	int err;

	if (esw_stats == NULL)
		return -ENOMEM;

	if ((adapter->ahw->op_mode != QLCNIC_MGMT_FUNC) &&
	    (func != adapter->ahw->pci_func)) {
		dev_err(&adapter->pdev->dev,
			"Not privilege to query stats for func=%d", func);
		return -EIO;
	}

	stats_addr = dma_alloc_coherent(&adapter->pdev->dev, stats_size,
					&stats_dma_t, GFP_KERNEL | __GFP_ZERO);
	if (!stats_addr)
		return -ENOMEM;

	arg1 = func | QLCNIC_STATS_VERSION << 8 | QLCNIC_STATS_PORT << 12;
	arg1 |= rx_tx << 15 | stats_size << 16;

	err = qlcnic_alloc_mbx_args(&cmd, adapter,
				    QLCNIC_CMD_GET_ESWITCH_STATS);
	if (err)
		goto out_free_dma;

	cmd.req.arg[1] = arg1;
	cmd.req.arg[2] = MSD(stats_dma_t);
	cmd.req.arg[3] = LSD(stats_dma_t);
	err = qlcnic_issue_cmd(adapter, &cmd);

	if (!err) {
		stats = stats_addr;
		esw_stats->context_id = le16_to_cpu(stats->context_id);
		esw_stats->version = le16_to_cpu(stats->version);
		esw_stats->size = le16_to_cpu(stats->size);
		esw_stats->multicast_frames =
				le64_to_cpu(stats->multicast_frames);
		esw_stats->broadcast_frames =
				le64_to_cpu(stats->broadcast_frames);
		esw_stats->unicast_frames = le64_to_cpu(stats->unicast_frames);
		esw_stats->dropped_frames = le64_to_cpu(stats->dropped_frames);
		esw_stats->local_frames = le64_to_cpu(stats->local_frames);
		esw_stats->errors = le64_to_cpu(stats->errors);
		esw_stats->numbytes = le64_to_cpu(stats->numbytes);
	}

	qlcnic_free_mbx_args(&cmd);
out_free_dma:
	dma_free_coherent(&adapter->pdev->dev, stats_size, stats_addr,
			  stats_dma_t);

	return err;
}

/* This routine will retrieve the MAC statistics from firmware */
int qlcnic_get_mac_stats(struct qlcnic_adapter *adapter,
		struct qlcnic_mac_statistics *mac_stats)
{
	struct qlcnic_mac_statistics_le *stats;
	struct qlcnic_cmd_args cmd;
	size_t stats_size = sizeof(struct qlcnic_mac_statistics_le);
	dma_addr_t stats_dma_t;
	void *stats_addr;
	int err;

	if (mac_stats == NULL)
		return -ENOMEM;

	stats_addr = dma_alloc_coherent(&adapter->pdev->dev, stats_size,
					&stats_dma_t, GFP_KERNEL | __GFP_ZERO);
	if (!stats_addr)
		return -ENOMEM;

	err = qlcnic_alloc_mbx_args(&cmd, adapter, QLCNIC_CMD_GET_MAC_STATS);
	if (err)
		goto out_free_dma;

	cmd.req.arg[1] = stats_size << 16;
	cmd.req.arg[2] = MSD(stats_dma_t);
	cmd.req.arg[3] = LSD(stats_dma_t);
	err = qlcnic_issue_cmd(adapter, &cmd);
	if (!err) {
		stats = stats_addr;
		mac_stats->mac_tx_frames = le64_to_cpu(stats->mac_tx_frames);
		mac_stats->mac_tx_bytes = le64_to_cpu(stats->mac_tx_bytes);
		mac_stats->mac_tx_mcast_pkts =
					le64_to_cpu(stats->mac_tx_mcast_pkts);
		mac_stats->mac_tx_bcast_pkts =
					le64_to_cpu(stats->mac_tx_bcast_pkts);
		mac_stats->mac_rx_frames = le64_to_cpu(stats->mac_rx_frames);
		mac_stats->mac_rx_bytes = le64_to_cpu(stats->mac_rx_bytes);
		mac_stats->mac_rx_mcast_pkts =
					le64_to_cpu(stats->mac_rx_mcast_pkts);
		mac_stats->mac_rx_length_error =
				le64_to_cpu(stats->mac_rx_length_error);
		mac_stats->mac_rx_length_small =
				le64_to_cpu(stats->mac_rx_length_small);
		mac_stats->mac_rx_length_large =
				le64_to_cpu(stats->mac_rx_length_large);
		mac_stats->mac_rx_jabber = le64_to_cpu(stats->mac_rx_jabber);
		mac_stats->mac_rx_dropped = le64_to_cpu(stats->mac_rx_dropped);
		mac_stats->mac_rx_crc_error = le64_to_cpu(stats->mac_rx_crc_error);
	} else {
		dev_err(&adapter->pdev->dev,
			"%s: Get mac stats failed, err=%d.\n", __func__, err);
	}

	qlcnic_free_mbx_args(&cmd);

out_free_dma:
	dma_free_coherent(&adapter->pdev->dev, stats_size, stats_addr,
			  stats_dma_t);

	return err;
}

int qlcnic_get_eswitch_stats(struct qlcnic_adapter *adapter, const u8 eswitch,
		const u8 rx_tx, struct __qlcnic_esw_statistics *esw_stats) {

	struct __qlcnic_esw_statistics port_stats;
	u8 i;
	int ret = -EIO;

	if (esw_stats == NULL)
		return -ENOMEM;
	if (adapter->ahw->op_mode != QLCNIC_MGMT_FUNC)
		return -EIO;
	if (adapter->npars == NULL)
		return -EIO;

	memset(esw_stats, 0, sizeof(u64));
	esw_stats->unicast_frames = QLCNIC_STATS_NOT_AVAIL;
	esw_stats->multicast_frames = QLCNIC_STATS_NOT_AVAIL;
	esw_stats->broadcast_frames = QLCNIC_STATS_NOT_AVAIL;
	esw_stats->dropped_frames = QLCNIC_STATS_NOT_AVAIL;
	esw_stats->errors = QLCNIC_STATS_NOT_AVAIL;
	esw_stats->local_frames = QLCNIC_STATS_NOT_AVAIL;
	esw_stats->numbytes = QLCNIC_STATS_NOT_AVAIL;
	esw_stats->context_id = eswitch;

	for (i = 0; i < adapter->ahw->act_pci_func; i++) {
		if (adapter->npars[i].phy_port != eswitch)
			continue;

		memset(&port_stats, 0, sizeof(struct __qlcnic_esw_statistics));
		if (qlcnic_get_port_stats(adapter, adapter->npars[i].pci_func,
					  rx_tx, &port_stats))
			continue;

		esw_stats->size = port_stats.size;
		esw_stats->version = port_stats.version;
		QLCNIC_ADD_ESW_STATS(esw_stats->unicast_frames,
						port_stats.unicast_frames);
		QLCNIC_ADD_ESW_STATS(esw_stats->multicast_frames,
						port_stats.multicast_frames);
		QLCNIC_ADD_ESW_STATS(esw_stats->broadcast_frames,
						port_stats.broadcast_frames);
		QLCNIC_ADD_ESW_STATS(esw_stats->dropped_frames,
						port_stats.dropped_frames);
		QLCNIC_ADD_ESW_STATS(esw_stats->errors,
						port_stats.errors);
		QLCNIC_ADD_ESW_STATS(esw_stats->local_frames,
						port_stats.local_frames);
		QLCNIC_ADD_ESW_STATS(esw_stats->numbytes,
						port_stats.numbytes);
		ret = 0;
	}
	return ret;
}

int qlcnic_clear_esw_stats(struct qlcnic_adapter *adapter, const u8 func_esw,
		const u8 port, const u8 rx_tx)
{
	int err;
	u32 arg1;
	struct qlcnic_cmd_args cmd;

	if (adapter->ahw->op_mode != QLCNIC_MGMT_FUNC)
		return -EIO;

	if (func_esw == QLCNIC_STATS_PORT) {
		if (port >= QLCNIC_MAX_PCI_FUNC)
			goto err_ret;
	} else if (func_esw == QLCNIC_STATS_ESWITCH) {
		if (port >= QLCNIC_NIU_MAX_XG_PORTS)
			goto err_ret;
	} else {
		goto err_ret;
	}

	if (rx_tx > QLCNIC_QUERY_TX_COUNTER)
		goto err_ret;

	arg1 = port | QLCNIC_STATS_VERSION << 8 | func_esw << 12;
	arg1 |= BIT_14 | rx_tx << 15;

	err = qlcnic_alloc_mbx_args(&cmd, adapter,
				    QLCNIC_CMD_GET_ESWITCH_STATS);
	if (err)
		return err;

	cmd.req.arg[1] = arg1;
	err = qlcnic_issue_cmd(adapter, &cmd);
	qlcnic_free_mbx_args(&cmd);
	return err;

err_ret:
	dev_err(&adapter->pdev->dev,
		"Invalid args func_esw %d port %d rx_ctx %d\n",
		func_esw, port, rx_tx);
	return -EIO;
}

static int __qlcnic_get_eswitch_port_config(struct qlcnic_adapter *adapter,
					    u32 *arg1, u32 *arg2)
{
	struct device *dev = &adapter->pdev->dev;
	struct qlcnic_cmd_args cmd;
	u8 pci_func = *arg1 >> 8;
	int err;

	err = qlcnic_alloc_mbx_args(&cmd, adapter,
				    QLCNIC_CMD_GET_ESWITCH_PORT_CONFIG);
	if (err)
		return err;

	cmd.req.arg[1] = *arg1;
	err = qlcnic_issue_cmd(adapter, &cmd);
	*arg1 = cmd.rsp.arg[1];
	*arg2 = cmd.rsp.arg[2];
	qlcnic_free_mbx_args(&cmd);

	if (err == QLCNIC_RCODE_SUCCESS)
		dev_info(dev, "Get eSwitch port config for vNIC function %d\n",
			 pci_func);
	else
		dev_err(dev, "Failed to get eswitch port config for vNIC function %d\n",
			pci_func);
	return err;
}
/* Configure eSwitch port
op_mode = 0 for setting default port behavior
op_mode = 1 for setting  vlan id
op_mode = 2 for deleting vlan id
op_type = 0 for vlan_id
op_type = 1 for port vlan_id
*/
int qlcnic_config_switch_port(struct qlcnic_adapter *adapter,
		struct qlcnic_esw_func_cfg *esw_cfg)
{
	struct device *dev = &adapter->pdev->dev;
	struct qlcnic_cmd_args cmd;
	int err = -EIO, index;
	u32 arg1, arg2 = 0;
	u8 pci_func;

	if (adapter->ahw->op_mode != QLCNIC_MGMT_FUNC)
		return err;
	pci_func = esw_cfg->pci_func;
	index = qlcnic_is_valid_nic_func(adapter, pci_func);
	if (index < 0)
		return err;
	arg1 = (adapter->npars[index].phy_port & BIT_0);
	arg1 |= (pci_func << 8);

	if (__qlcnic_get_eswitch_port_config(adapter, &arg1, &arg2))
		return err;
	arg1 &= ~(0x0ff << 8);
	arg1 |= (pci_func << 8);
	arg1 &= ~(BIT_2 | BIT_3);
	switch (esw_cfg->op_mode) {
	case QLCNIC_PORT_DEFAULTS:
		arg1 |= (BIT_4 | BIT_6 | BIT_7);
		arg2 |= (BIT_0 | BIT_1);
		if (adapter->ahw->capabilities & QLCNIC_FW_CAPABILITY_TSO)
			arg2 |= (BIT_2 | BIT_3);
		if (!(esw_cfg->discard_tagged))
			arg1 &= ~BIT_4;
		if (!(esw_cfg->promisc_mode))
			arg1 &= ~BIT_6;
		if (!(esw_cfg->mac_override))
			arg1 &= ~BIT_7;
		if (!(esw_cfg->mac_anti_spoof))
			arg2 &= ~BIT_0;
		if (!(esw_cfg->offload_flags & BIT_0))
			arg2 &= ~(BIT_1 | BIT_2 | BIT_3);
		if (!(esw_cfg->offload_flags & BIT_1))
			arg2 &= ~BIT_2;
		if (!(esw_cfg->offload_flags & BIT_2))
			arg2 &= ~BIT_3;
		break;
	case QLCNIC_ADD_VLAN:
			arg1 |= (BIT_2 | BIT_5);
			arg1 |= (esw_cfg->vlan_id << 16);
			break;
	case QLCNIC_DEL_VLAN:
			arg1 |= (BIT_3 | BIT_5);
			arg1 &= ~(0x0ffff << 16);
			break;
	default:
		return err;
	}

	err = qlcnic_alloc_mbx_args(&cmd, adapter,
				    QLCNIC_CMD_CONFIGURE_ESWITCH);
	if (err)
		return err;

	cmd.req.arg[1] = arg1;
	cmd.req.arg[2] = arg2;
	err = qlcnic_issue_cmd(adapter, &cmd);
	qlcnic_free_mbx_args(&cmd);

	if (err != QLCNIC_RCODE_SUCCESS)
		dev_err(dev, "Failed to configure eswitch for vNIC function %d\n",
			pci_func);
	else
		dev_info(dev, "Configured eSwitch for vNIC function %d\n",
			 pci_func);

	return err;
}

int
qlcnic_get_eswitch_port_config(struct qlcnic_adapter *adapter,
			struct qlcnic_esw_func_cfg *esw_cfg)
{
	u32 arg1, arg2;
	int index;
	u8 phy_port;

	if (adapter->ahw->op_mode == QLCNIC_MGMT_FUNC) {
		index = qlcnic_is_valid_nic_func(adapter, esw_cfg->pci_func);
		if (index < 0)
			return -EIO;
		phy_port = adapter->npars[index].phy_port;
	} else {
		phy_port = adapter->ahw->physical_port;
	}
	arg1 = phy_port;
	arg1 |= (esw_cfg->pci_func << 8);
	if (__qlcnic_get_eswitch_port_config(adapter, &arg1, &arg2))
		return -EIO;

	esw_cfg->discard_tagged = !!(arg1 & BIT_4);
	esw_cfg->host_vlan_tag = !!(arg1 & BIT_5);
	esw_cfg->promisc_mode = !!(arg1 & BIT_6);
	esw_cfg->mac_override = !!(arg1 & BIT_7);
	esw_cfg->vlan_id = LSW(arg1 >> 16);
	esw_cfg->mac_anti_spoof = (arg2 & 0x1);
	esw_cfg->offload_flags = ((arg2 >> 1) & 0x7);

	return 0;
}
