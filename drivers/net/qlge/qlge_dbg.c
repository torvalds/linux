#include <linux/slab.h>

#include "qlge.h"

/* Read a NIC register from the alternate function. */
static u32 ql_read_other_func_reg(struct ql_adapter *qdev,
						u32 reg)
{
	u32 register_to_read;
	u32 reg_val;
	unsigned int status = 0;

	register_to_read = MPI_NIC_REG_BLOCK
				| MPI_NIC_READ
				| (qdev->alt_func << MPI_NIC_FUNCTION_SHIFT)
				| reg;
	status = ql_read_mpi_reg(qdev, register_to_read, &reg_val);
	if (status != 0)
		return 0xffffffff;

	return reg_val;
}

/* Write a NIC register from the alternate function. */
static int ql_write_other_func_reg(struct ql_adapter *qdev,
					u32 reg, u32 reg_val)
{
	u32 register_to_read;
	int status = 0;

	register_to_read = MPI_NIC_REG_BLOCK
				| MPI_NIC_READ
				| (qdev->alt_func << MPI_NIC_FUNCTION_SHIFT)
				| reg;
	status = ql_write_mpi_reg(qdev, register_to_read, reg_val);

	return status;
}

static int ql_wait_other_func_reg_rdy(struct ql_adapter *qdev, u32 reg,
					u32 bit, u32 err_bit)
{
	u32 temp;
	int count = 10;

	while (count) {
		temp = ql_read_other_func_reg(qdev, reg);

		/* check for errors */
		if (temp & err_bit)
			return -1;
		else if (temp & bit)
			return 0;
		mdelay(10);
		count--;
	}
	return -1;
}

static int ql_read_other_func_serdes_reg(struct ql_adapter *qdev, u32 reg,
							u32 *data)
{
	int status;

	/* wait for reg to come ready */
	status = ql_wait_other_func_reg_rdy(qdev, XG_SERDES_ADDR / 4,
						XG_SERDES_ADDR_RDY, 0);
	if (status)
		goto exit;

	/* set up for reg read */
	ql_write_other_func_reg(qdev, XG_SERDES_ADDR/4, reg | PROC_ADDR_R);

	/* wait for reg to come ready */
	status = ql_wait_other_func_reg_rdy(qdev, XG_SERDES_ADDR / 4,
						XG_SERDES_ADDR_RDY, 0);
	if (status)
		goto exit;

	/* get the data */
	*data = ql_read_other_func_reg(qdev, (XG_SERDES_DATA / 4));
exit:
	return status;
}

/* Read out the SERDES registers */
static int ql_read_serdes_reg(struct ql_adapter *qdev, u32 reg, u32 * data)
{
	int status;

	/* wait for reg to come ready */
	status = ql_wait_reg_rdy(qdev, XG_SERDES_ADDR, XG_SERDES_ADDR_RDY, 0);
	if (status)
		goto exit;

	/* set up for reg read */
	ql_write32(qdev, XG_SERDES_ADDR, reg | PROC_ADDR_R);

	/* wait for reg to come ready */
	status = ql_wait_reg_rdy(qdev, XG_SERDES_ADDR, XG_SERDES_ADDR_RDY, 0);
	if (status)
		goto exit;

	/* get the data */
	*data = ql_read32(qdev, XG_SERDES_DATA);
exit:
	return status;
}

static void ql_get_both_serdes(struct ql_adapter *qdev, u32 addr,
			u32 *direct_ptr, u32 *indirect_ptr,
			unsigned int direct_valid, unsigned int indirect_valid)
{
	unsigned int status;

	status = 1;
	if (direct_valid)
		status = ql_read_serdes_reg(qdev, addr, direct_ptr);
	/* Dead fill any failures or invalids. */
	if (status)
		*direct_ptr = 0xDEADBEEF;

	status = 1;
	if (indirect_valid)
		status = ql_read_other_func_serdes_reg(
						qdev, addr, indirect_ptr);
	/* Dead fill any failures or invalids. */
	if (status)
		*indirect_ptr = 0xDEADBEEF;
}

static int ql_get_serdes_regs(struct ql_adapter *qdev,
				struct ql_mpi_coredump *mpi_coredump)
{
	int status;
	unsigned int xfi_direct_valid, xfi_indirect_valid, xaui_direct_valid;
	unsigned int xaui_indirect_valid, i;
	u32 *direct_ptr, temp;
	u32 *indirect_ptr;

	xfi_direct_valid = xfi_indirect_valid = 0;
	xaui_direct_valid = xaui_indirect_valid = 1;

	/* The XAUI needs to be read out per port */
	if (qdev->func & 1) {
		/* We are NIC 2	*/
		status = ql_read_other_func_serdes_reg(qdev,
				XG_SERDES_XAUI_HSS_PCS_START, &temp);
		if (status)
			temp = XG_SERDES_ADDR_XAUI_PWR_DOWN;
		if ((temp & XG_SERDES_ADDR_XAUI_PWR_DOWN) ==
					XG_SERDES_ADDR_XAUI_PWR_DOWN)
			xaui_indirect_valid = 0;

		status = ql_read_serdes_reg(qdev,
				XG_SERDES_XAUI_HSS_PCS_START, &temp);
		if (status)
			temp = XG_SERDES_ADDR_XAUI_PWR_DOWN;

		if ((temp & XG_SERDES_ADDR_XAUI_PWR_DOWN) ==
					XG_SERDES_ADDR_XAUI_PWR_DOWN)
			xaui_direct_valid = 0;
	} else {
		/* We are NIC 1	*/
		status = ql_read_other_func_serdes_reg(qdev,
				XG_SERDES_XAUI_HSS_PCS_START, &temp);
		if (status)
			temp = XG_SERDES_ADDR_XAUI_PWR_DOWN;
		if ((temp & XG_SERDES_ADDR_XAUI_PWR_DOWN) ==
					XG_SERDES_ADDR_XAUI_PWR_DOWN)
			xaui_indirect_valid = 0;

		status = ql_read_serdes_reg(qdev,
				XG_SERDES_XAUI_HSS_PCS_START, &temp);
		if (status)
			temp = XG_SERDES_ADDR_XAUI_PWR_DOWN;
		if ((temp & XG_SERDES_ADDR_XAUI_PWR_DOWN) ==
					XG_SERDES_ADDR_XAUI_PWR_DOWN)
			xaui_direct_valid = 0;
	}

	/*
	 * XFI register is shared so only need to read one
	 * functions and then check the bits.
	 */
	status = ql_read_serdes_reg(qdev, XG_SERDES_ADDR_STS, &temp);
	if (status)
		temp = 0;

	if ((temp & XG_SERDES_ADDR_XFI1_PWR_UP) ==
					XG_SERDES_ADDR_XFI1_PWR_UP) {
		/* now see if i'm NIC 1 or NIC 2 */
		if (qdev->func & 1)
			/* I'm NIC 2, so the indirect (NIC1) xfi is up. */
			xfi_indirect_valid = 1;
		else
			xfi_direct_valid = 1;
	}
	if ((temp & XG_SERDES_ADDR_XFI2_PWR_UP) ==
					XG_SERDES_ADDR_XFI2_PWR_UP) {
		/* now see if i'm NIC 1 or NIC 2 */
		if (qdev->func & 1)
			/* I'm NIC 2, so the indirect (NIC1) xfi is up. */
			xfi_direct_valid = 1;
		else
			xfi_indirect_valid = 1;
	}

	/* Get XAUI_AN register block. */
	if (qdev->func & 1) {
		/* Function 2 is direct	*/
		direct_ptr = mpi_coredump->serdes2_xaui_an;
		indirect_ptr = mpi_coredump->serdes_xaui_an;
	} else {
		/* Function 1 is direct	*/
		direct_ptr = mpi_coredump->serdes_xaui_an;
		indirect_ptr = mpi_coredump->serdes2_xaui_an;
	}

	for (i = 0; i <= 0x000000034; i += 4, direct_ptr++, indirect_ptr++)
		ql_get_both_serdes(qdev, i, direct_ptr, indirect_ptr,
					xaui_direct_valid, xaui_indirect_valid);

	/* Get XAUI_HSS_PCS register block. */
	if (qdev->func & 1) {
		direct_ptr =
			mpi_coredump->serdes2_xaui_hss_pcs;
		indirect_ptr =
			mpi_coredump->serdes_xaui_hss_pcs;
	} else {
		direct_ptr =
			mpi_coredump->serdes_xaui_hss_pcs;
		indirect_ptr =
			mpi_coredump->serdes2_xaui_hss_pcs;
	}

	for (i = 0x800; i <= 0x880; i += 4, direct_ptr++, indirect_ptr++)
		ql_get_both_serdes(qdev, i, direct_ptr, indirect_ptr,
					xaui_direct_valid, xaui_indirect_valid);

	/* Get XAUI_XFI_AN register block. */
	if (qdev->func & 1) {
		direct_ptr = mpi_coredump->serdes2_xfi_an;
		indirect_ptr = mpi_coredump->serdes_xfi_an;
	} else {
		direct_ptr = mpi_coredump->serdes_xfi_an;
		indirect_ptr = mpi_coredump->serdes2_xfi_an;
	}

	for (i = 0x1000; i <= 0x1034; i += 4, direct_ptr++, indirect_ptr++)
		ql_get_both_serdes(qdev, i, direct_ptr, indirect_ptr,
					xfi_direct_valid, xfi_indirect_valid);

	/* Get XAUI_XFI_TRAIN register block. */
	if (qdev->func & 1) {
		direct_ptr = mpi_coredump->serdes2_xfi_train;
		indirect_ptr =
			mpi_coredump->serdes_xfi_train;
	} else {
		direct_ptr = mpi_coredump->serdes_xfi_train;
		indirect_ptr =
			mpi_coredump->serdes2_xfi_train;
	}

	for (i = 0x1050; i <= 0x107c; i += 4, direct_ptr++, indirect_ptr++)
		ql_get_both_serdes(qdev, i, direct_ptr, indirect_ptr,
					xfi_direct_valid, xfi_indirect_valid);

	/* Get XAUI_XFI_HSS_PCS register block. */
	if (qdev->func & 1) {
		direct_ptr =
			mpi_coredump->serdes2_xfi_hss_pcs;
		indirect_ptr =
			mpi_coredump->serdes_xfi_hss_pcs;
	} else {
		direct_ptr =
			mpi_coredump->serdes_xfi_hss_pcs;
		indirect_ptr =
			mpi_coredump->serdes2_xfi_hss_pcs;
	}

	for (i = 0x1800; i <= 0x1838; i += 4, direct_ptr++, indirect_ptr++)
		ql_get_both_serdes(qdev, i, direct_ptr, indirect_ptr,
					xfi_direct_valid, xfi_indirect_valid);

	/* Get XAUI_XFI_HSS_TX register block. */
	if (qdev->func & 1) {
		direct_ptr =
			mpi_coredump->serdes2_xfi_hss_tx;
		indirect_ptr =
			mpi_coredump->serdes_xfi_hss_tx;
	} else {
		direct_ptr = mpi_coredump->serdes_xfi_hss_tx;
		indirect_ptr =
			mpi_coredump->serdes2_xfi_hss_tx;
	}
	for (i = 0x1c00; i <= 0x1c1f; i++, direct_ptr++, indirect_ptr++)
		ql_get_both_serdes(qdev, i, direct_ptr, indirect_ptr,
					xfi_direct_valid, xfi_indirect_valid);

	/* Get XAUI_XFI_HSS_RX register block. */
	if (qdev->func & 1) {
		direct_ptr =
			mpi_coredump->serdes2_xfi_hss_rx;
		indirect_ptr =
			mpi_coredump->serdes_xfi_hss_rx;
	} else {
		direct_ptr = mpi_coredump->serdes_xfi_hss_rx;
		indirect_ptr =
			mpi_coredump->serdes2_xfi_hss_rx;
	}

	for (i = 0x1c40; i <= 0x1c5f; i++, direct_ptr++, indirect_ptr++)
		ql_get_both_serdes(qdev, i, direct_ptr, indirect_ptr,
					xfi_direct_valid, xfi_indirect_valid);


	/* Get XAUI_XFI_HSS_PLL register block. */
	if (qdev->func & 1) {
		direct_ptr =
			mpi_coredump->serdes2_xfi_hss_pll;
		indirect_ptr =
			mpi_coredump->serdes_xfi_hss_pll;
	} else {
		direct_ptr =
			mpi_coredump->serdes_xfi_hss_pll;
		indirect_ptr =
			mpi_coredump->serdes2_xfi_hss_pll;
	}
	for (i = 0x1e00; i <= 0x1e1f; i++, direct_ptr++, indirect_ptr++)
		ql_get_both_serdes(qdev, i, direct_ptr, indirect_ptr,
					xfi_direct_valid, xfi_indirect_valid);
	return 0;
}

static int ql_read_other_func_xgmac_reg(struct ql_adapter *qdev, u32 reg,
							u32 *data)
{
	int status = 0;

	/* wait for reg to come ready */
	status = ql_wait_other_func_reg_rdy(qdev, XGMAC_ADDR / 4,
						XGMAC_ADDR_RDY, XGMAC_ADDR_XME);
	if (status)
		goto exit;

	/* set up for reg read */
	ql_write_other_func_reg(qdev, XGMAC_ADDR / 4, reg | XGMAC_ADDR_R);

	/* wait for reg to come ready */
	status = ql_wait_other_func_reg_rdy(qdev, XGMAC_ADDR / 4,
						XGMAC_ADDR_RDY, XGMAC_ADDR_XME);
	if (status)
		goto exit;

	/* get the data */
	*data = ql_read_other_func_reg(qdev, XGMAC_DATA / 4);
exit:
	return status;
}

/* Read the 400 xgmac control/statistics registers
 * skipping unused locations.
 */
static int ql_get_xgmac_regs(struct ql_adapter *qdev, u32 * buf,
					unsigned int other_function)
{
	int status = 0;
	int i;

	for (i = PAUSE_SRC_LO; i < XGMAC_REGISTER_END; i += 4, buf++) {
		/* We're reading 400 xgmac registers, but we filter out
		 * serveral locations that are non-responsive to reads.
		 */
		if ((i == 0x00000114) ||
			(i == 0x00000118) ||
			(i == 0x0000013c) ||
			(i == 0x00000140) ||
			(i > 0x00000150 && i < 0x000001fc) ||
			(i > 0x00000278 && i < 0x000002a0) ||
			(i > 0x000002c0 && i < 0x000002cf) ||
			(i > 0x000002dc && i < 0x000002f0) ||
			(i > 0x000003c8 && i < 0x00000400) ||
			(i > 0x00000400 && i < 0x00000410) ||
			(i > 0x00000410 && i < 0x00000420) ||
			(i > 0x00000420 && i < 0x00000430) ||
			(i > 0x00000430 && i < 0x00000440) ||
			(i > 0x00000440 && i < 0x00000450) ||
			(i > 0x00000450 && i < 0x00000500) ||
			(i > 0x0000054c && i < 0x00000568) ||
			(i > 0x000005c8 && i < 0x00000600)) {
			if (other_function)
				status =
				ql_read_other_func_xgmac_reg(qdev, i, buf);
			else
				status = ql_read_xgmac_reg(qdev, i, buf);

			if (status)
				*buf = 0xdeadbeef;
			break;
		}
	}
	return status;
}

static int ql_get_ets_regs(struct ql_adapter *qdev, u32 * buf)
{
	int status = 0;
	int i;

	for (i = 0; i < 8; i++, buf++) {
		ql_write32(qdev, NIC_ETS, i << 29 | 0x08000000);
		*buf = ql_read32(qdev, NIC_ETS);
	}

	for (i = 0; i < 2; i++, buf++) {
		ql_write32(qdev, CNA_ETS, i << 29 | 0x08000000);
		*buf = ql_read32(qdev, CNA_ETS);
	}

	return status;
}

static void ql_get_intr_states(struct ql_adapter *qdev, u32 * buf)
{
	int i;

	for (i = 0; i < qdev->rx_ring_count; i++, buf++) {
		ql_write32(qdev, INTR_EN,
				qdev->intr_context[i].intr_read_mask);
		*buf = ql_read32(qdev, INTR_EN);
	}
}

static int ql_get_cam_entries(struct ql_adapter *qdev, u32 * buf)
{
	int i, status;
	u32 value[3];

	status = ql_sem_spinlock(qdev, SEM_MAC_ADDR_MASK);
	if (status)
		return status;

	for (i = 0; i < 16; i++) {
		status = ql_get_mac_addr_reg(qdev,
					MAC_ADDR_TYPE_CAM_MAC, i, value);
		if (status) {
			netif_err(qdev, drv, qdev->ndev,
				  "Failed read of mac index register.\n");
			goto err;
		}
		*buf++ = value[0];	/* lower MAC address */
		*buf++ = value[1];	/* upper MAC address */
		*buf++ = value[2];	/* output */
	}
	for (i = 0; i < 32; i++) {
		status = ql_get_mac_addr_reg(qdev,
					MAC_ADDR_TYPE_MULTI_MAC, i, value);
		if (status) {
			netif_err(qdev, drv, qdev->ndev,
				  "Failed read of mac index register.\n");
			goto err;
		}
		*buf++ = value[0];	/* lower Mcast address */
		*buf++ = value[1];	/* upper Mcast address */
	}
err:
	ql_sem_unlock(qdev, SEM_MAC_ADDR_MASK);
	return status;
}

static int ql_get_routing_entries(struct ql_adapter *qdev, u32 * buf)
{
	int status;
	u32 value, i;

	status = ql_sem_spinlock(qdev, SEM_RT_IDX_MASK);
	if (status)
		return status;

	for (i = 0; i < 16; i++) {
		status = ql_get_routing_reg(qdev, i, &value);
		if (status) {
			netif_err(qdev, drv, qdev->ndev,
				  "Failed read of routing index register.\n");
			goto err;
		} else {
			*buf++ = value;
		}
	}
err:
	ql_sem_unlock(qdev, SEM_RT_IDX_MASK);
	return status;
}

/* Read the MPI Processor shadow registers */
static int ql_get_mpi_shadow_regs(struct ql_adapter *qdev, u32 * buf)
{
	u32 i;
	int status;

	for (i = 0; i < MPI_CORE_SH_REGS_CNT; i++, buf++) {
		status = ql_write_mpi_reg(qdev, RISC_124,
				(SHADOW_OFFSET | i << SHADOW_REG_SHIFT));
		if (status)
			goto end;
		status = ql_read_mpi_reg(qdev, RISC_127, buf);
		if (status)
			goto end;
	}
end:
	return status;
}

/* Read the MPI Processor core registers */
static int ql_get_mpi_regs(struct ql_adapter *qdev, u32 * buf,
				u32 offset, u32 count)
{
	int i, status = 0;
	for (i = 0; i < count; i++, buf++) {
		status = ql_read_mpi_reg(qdev, offset + i, buf);
		if (status)
			return status;
	}
	return status;
}

/* Read the ASIC probe dump */
static unsigned int *ql_get_probe(struct ql_adapter *qdev, u32 clock,
					u32 valid, u32 *buf)
{
	u32 module, mux_sel, probe, lo_val, hi_val;

	for (module = 0; module < PRB_MX_ADDR_MAX_MODS; module++) {
		if (!((valid >> module) & 1))
			continue;
		for (mux_sel = 0; mux_sel < PRB_MX_ADDR_MAX_MUX; mux_sel++) {
			probe = clock
				| PRB_MX_ADDR_ARE
				| mux_sel
				| (module << PRB_MX_ADDR_MOD_SEL_SHIFT);
			ql_write32(qdev, PRB_MX_ADDR, probe);
			lo_val = ql_read32(qdev, PRB_MX_DATA);
			if (mux_sel == 0) {
				*buf = probe;
				buf++;
			}
			probe |= PRB_MX_ADDR_UP;
			ql_write32(qdev, PRB_MX_ADDR, probe);
			hi_val = ql_read32(qdev, PRB_MX_DATA);
			*buf = lo_val;
			buf++;
			*buf = hi_val;
			buf++;
		}
	}
	return buf;
}

static int ql_get_probe_dump(struct ql_adapter *qdev, unsigned int *buf)
{
	/* First we have to enable the probe mux */
	ql_write_mpi_reg(qdev, MPI_TEST_FUNC_PRB_CTL, MPI_TEST_FUNC_PRB_EN);
	buf = ql_get_probe(qdev, PRB_MX_ADDR_SYS_CLOCK,
			PRB_MX_ADDR_VALID_SYS_MOD, buf);
	buf = ql_get_probe(qdev, PRB_MX_ADDR_PCI_CLOCK,
			PRB_MX_ADDR_VALID_PCI_MOD, buf);
	buf = ql_get_probe(qdev, PRB_MX_ADDR_XGM_CLOCK,
			PRB_MX_ADDR_VALID_XGM_MOD, buf);
	buf = ql_get_probe(qdev, PRB_MX_ADDR_FC_CLOCK,
			PRB_MX_ADDR_VALID_FC_MOD, buf);
	return 0;

}

/* Read out the routing index registers */
static int ql_get_routing_index_registers(struct ql_adapter *qdev, u32 *buf)
{
	int status;
	u32 type, index, index_max;
	u32 result_index;
	u32 result_data;
	u32 val;

	status = ql_sem_spinlock(qdev, SEM_RT_IDX_MASK);
	if (status)
		return status;

	for (type = 0; type < 4; type++) {
		if (type < 2)
			index_max = 8;
		else
			index_max = 16;
		for (index = 0; index < index_max; index++) {
			val = RT_IDX_RS
				| (type << RT_IDX_TYPE_SHIFT)
				| (index << RT_IDX_IDX_SHIFT);
			ql_write32(qdev, RT_IDX, val);
			result_index = 0;
			while ((result_index & RT_IDX_MR) == 0)
				result_index = ql_read32(qdev, RT_IDX);
			result_data = ql_read32(qdev, RT_DATA);
			*buf = type;
			buf++;
			*buf = index;
			buf++;
			*buf = result_index;
			buf++;
			*buf = result_data;
			buf++;
		}
	}
	ql_sem_unlock(qdev, SEM_RT_IDX_MASK);
	return status;
}

/* Read out the MAC protocol registers */
static void ql_get_mac_protocol_registers(struct ql_adapter *qdev, u32 *buf)
{
	u32 result_index, result_data;
	u32 type;
	u32 index;
	u32 offset;
	u32 val;
	u32 initial_val = MAC_ADDR_RS;
	u32 max_index;
	u32 max_offset;

	for (type = 0; type < MAC_ADDR_TYPE_COUNT; type++) {
		switch (type) {

		case 0: /* CAM */
			initial_val |= MAC_ADDR_ADR;
			max_index = MAC_ADDR_MAX_CAM_ENTRIES;
			max_offset = MAC_ADDR_MAX_CAM_WCOUNT;
			break;
		case 1: /* Multicast MAC Address */
			max_index = MAC_ADDR_MAX_CAM_WCOUNT;
			max_offset = MAC_ADDR_MAX_CAM_WCOUNT;
			break;
		case 2: /* VLAN filter mask */
		case 3: /* MC filter mask */
			max_index = MAC_ADDR_MAX_CAM_WCOUNT;
			max_offset = MAC_ADDR_MAX_CAM_WCOUNT;
			break;
		case 4: /* FC MAC addresses */
			max_index = MAC_ADDR_MAX_FC_MAC_ENTRIES;
			max_offset = MAC_ADDR_MAX_FC_MAC_WCOUNT;
			break;
		case 5: /* Mgmt MAC addresses */
			max_index = MAC_ADDR_MAX_MGMT_MAC_ENTRIES;
			max_offset = MAC_ADDR_MAX_MGMT_MAC_WCOUNT;
			break;
		case 6: /* Mgmt VLAN addresses */
			max_index = MAC_ADDR_MAX_MGMT_VLAN_ENTRIES;
			max_offset = MAC_ADDR_MAX_MGMT_VLAN_WCOUNT;
			break;
		case 7: /* Mgmt IPv4 address */
			max_index = MAC_ADDR_MAX_MGMT_V4_ENTRIES;
			max_offset = MAC_ADDR_MAX_MGMT_V4_WCOUNT;
			break;
		case 8: /* Mgmt IPv6 address */
			max_index = MAC_ADDR_MAX_MGMT_V6_ENTRIES;
			max_offset = MAC_ADDR_MAX_MGMT_V6_WCOUNT;
			break;
		case 9: /* Mgmt TCP/UDP Dest port */
			max_index = MAC_ADDR_MAX_MGMT_TU_DP_ENTRIES;
			max_offset = MAC_ADDR_MAX_MGMT_TU_DP_WCOUNT;
			break;
		default:
			printk(KERN_ERR"Bad type!!! 0x%08x\n", type);
			max_index = 0;
			max_offset = 0;
			break;
		}
		for (index = 0; index < max_index; index++) {
			for (offset = 0; offset < max_offset; offset++) {
				val = initial_val
					| (type << MAC_ADDR_TYPE_SHIFT)
					| (index << MAC_ADDR_IDX_SHIFT)
					| (offset);
				ql_write32(qdev, MAC_ADDR_IDX, val);
				result_index = 0;
				while ((result_index & MAC_ADDR_MR) == 0) {
					result_index = ql_read32(qdev,
								MAC_ADDR_IDX);
				}
				result_data = ql_read32(qdev, MAC_ADDR_DATA);
				*buf = result_index;
				buf++;
				*buf = result_data;
				buf++;
			}
		}
	}
}

static void ql_get_sem_registers(struct ql_adapter *qdev, u32 *buf)
{
	u32 func_num, reg, reg_val;
	int status;

	for (func_num = 0; func_num < MAX_SEMAPHORE_FUNCTIONS ; func_num++) {
		reg = MPI_NIC_REG_BLOCK
			| (func_num << MPI_NIC_FUNCTION_SHIFT)
			| (SEM / 4);
		status = ql_read_mpi_reg(qdev, reg, &reg_val);
		*buf = reg_val;
		/* if the read failed then dead fill the element. */
		if (!status)
			*buf = 0xdeadbeef;
		buf++;
	}
}

/* Create a coredump segment header */
static void ql_build_coredump_seg_header(
		struct mpi_coredump_segment_header *seg_hdr,
		u32 seg_number, u32 seg_size, u8 *desc)
{
	memset(seg_hdr, 0, sizeof(struct mpi_coredump_segment_header));
	seg_hdr->cookie = MPI_COREDUMP_COOKIE;
	seg_hdr->segNum = seg_number;
	seg_hdr->segSize = seg_size;
	memcpy(seg_hdr->description, desc, (sizeof(seg_hdr->description)) - 1);
}

/*
 * This function should be called when a coredump / probedump
 * is to be extracted from the HBA. It is assumed there is a
 * qdev structure that contains the base address of the register
 * space for this function as well as a coredump structure that
 * will contain the dump.
 */
int ql_core_dump(struct ql_adapter *qdev, struct ql_mpi_coredump *mpi_coredump)
{
	int status;
	int i;

	if (!mpi_coredump) {
		netif_err(qdev, drv, qdev->ndev, "No memory available.\n");
		return -ENOMEM;
	}

	/* Try to get the spinlock, but dont worry if
	 * it isn't available.  If the firmware died it
	 * might be holding the sem.
	 */
	ql_sem_spinlock(qdev, SEM_PROC_REG_MASK);

	status = ql_pause_mpi_risc(qdev);
	if (status) {
		netif_err(qdev, drv, qdev->ndev,
			  "Failed RISC pause. Status = 0x%.08x\n", status);
		goto err;
	}

	/* Insert the global header */
	memset(&(mpi_coredump->mpi_global_header), 0,
		sizeof(struct mpi_coredump_global_header));
	mpi_coredump->mpi_global_header.cookie = MPI_COREDUMP_COOKIE;
	mpi_coredump->mpi_global_header.headerSize =
		sizeof(struct mpi_coredump_global_header);
	mpi_coredump->mpi_global_header.imageSize =
		sizeof(struct ql_mpi_coredump);
	memcpy(mpi_coredump->mpi_global_header.idString, "MPI Coredump",
		sizeof(mpi_coredump->mpi_global_header.idString));

	/* Get generic NIC reg dump */
	ql_build_coredump_seg_header(&mpi_coredump->nic_regs_seg_hdr,
			NIC1_CONTROL_SEG_NUM,
			sizeof(struct mpi_coredump_segment_header) +
			sizeof(mpi_coredump->nic_regs), "NIC1 Registers");

	ql_build_coredump_seg_header(&mpi_coredump->nic2_regs_seg_hdr,
			NIC2_CONTROL_SEG_NUM,
			sizeof(struct mpi_coredump_segment_header) +
			sizeof(mpi_coredump->nic2_regs), "NIC2 Registers");

	/* Get XGMac registers. (Segment 18, Rev C. step 21) */
	ql_build_coredump_seg_header(&mpi_coredump->xgmac1_seg_hdr,
			NIC1_XGMAC_SEG_NUM,
			sizeof(struct mpi_coredump_segment_header) +
			sizeof(mpi_coredump->xgmac1), "NIC1 XGMac Registers");

	ql_build_coredump_seg_header(&mpi_coredump->xgmac2_seg_hdr,
			NIC2_XGMAC_SEG_NUM,
			sizeof(struct mpi_coredump_segment_header) +
			sizeof(mpi_coredump->xgmac2), "NIC2 XGMac Registers");

	if (qdev->func & 1) {
		/* Odd means our function is NIC 2 */
		for (i = 0; i < NIC_REGS_DUMP_WORD_COUNT; i++)
			mpi_coredump->nic2_regs[i] =
					 ql_read32(qdev, i * sizeof(u32));

		for (i = 0; i < NIC_REGS_DUMP_WORD_COUNT; i++)
			mpi_coredump->nic_regs[i] =
			ql_read_other_func_reg(qdev, (i * sizeof(u32)) / 4);

		ql_get_xgmac_regs(qdev, &mpi_coredump->xgmac2[0], 0);
		ql_get_xgmac_regs(qdev, &mpi_coredump->xgmac1[0], 1);
	} else {
		/* Even means our function is NIC 1 */
		for (i = 0; i < NIC_REGS_DUMP_WORD_COUNT; i++)
			mpi_coredump->nic_regs[i] =
					ql_read32(qdev, i * sizeof(u32));
		for (i = 0; i < NIC_REGS_DUMP_WORD_COUNT; i++)
			mpi_coredump->nic2_regs[i] =
			ql_read_other_func_reg(qdev, (i * sizeof(u32)) / 4);

		ql_get_xgmac_regs(qdev, &mpi_coredump->xgmac1[0], 0);
		ql_get_xgmac_regs(qdev, &mpi_coredump->xgmac2[0], 1);
	}

	/* Rev C. Step 20a */
	ql_build_coredump_seg_header(&mpi_coredump->xaui_an_hdr,
			XAUI_AN_SEG_NUM,
			sizeof(struct mpi_coredump_segment_header) +
			sizeof(mpi_coredump->serdes_xaui_an),
			"XAUI AN Registers");

	/* Rev C. Step 20b */
	ql_build_coredump_seg_header(&mpi_coredump->xaui_hss_pcs_hdr,
			XAUI_HSS_PCS_SEG_NUM,
			sizeof(struct mpi_coredump_segment_header) +
			sizeof(mpi_coredump->serdes_xaui_hss_pcs),
			"XAUI HSS PCS Registers");

	ql_build_coredump_seg_header(&mpi_coredump->xfi_an_hdr, XFI_AN_SEG_NUM,
			sizeof(struct mpi_coredump_segment_header) +
			sizeof(mpi_coredump->serdes_xfi_an),
			"XFI AN Registers");

	ql_build_coredump_seg_header(&mpi_coredump->xfi_train_hdr,
			XFI_TRAIN_SEG_NUM,
			sizeof(struct mpi_coredump_segment_header) +
			sizeof(mpi_coredump->serdes_xfi_train),
			"XFI TRAIN Registers");

	ql_build_coredump_seg_header(&mpi_coredump->xfi_hss_pcs_hdr,
			XFI_HSS_PCS_SEG_NUM,
			sizeof(struct mpi_coredump_segment_header) +
			sizeof(mpi_coredump->serdes_xfi_hss_pcs),
			"XFI HSS PCS Registers");

	ql_build_coredump_seg_header(&mpi_coredump->xfi_hss_tx_hdr,
			XFI_HSS_TX_SEG_NUM,
			sizeof(struct mpi_coredump_segment_header) +
			sizeof(mpi_coredump->serdes_xfi_hss_tx),
			"XFI HSS TX Registers");

	ql_build_coredump_seg_header(&mpi_coredump->xfi_hss_rx_hdr,
			XFI_HSS_RX_SEG_NUM,
			sizeof(struct mpi_coredump_segment_header) +
			sizeof(mpi_coredump->serdes_xfi_hss_rx),
			"XFI HSS RX Registers");

	ql_build_coredump_seg_header(&mpi_coredump->xfi_hss_pll_hdr,
			XFI_HSS_PLL_SEG_NUM,
			sizeof(struct mpi_coredump_segment_header) +
			sizeof(mpi_coredump->serdes_xfi_hss_pll),
			"XFI HSS PLL Registers");

	ql_build_coredump_seg_header(&mpi_coredump->xaui2_an_hdr,
			XAUI2_AN_SEG_NUM,
			sizeof(struct mpi_coredump_segment_header) +
			sizeof(mpi_coredump->serdes2_xaui_an),
			"XAUI2 AN Registers");

	ql_build_coredump_seg_header(&mpi_coredump->xaui2_hss_pcs_hdr,
			XAUI2_HSS_PCS_SEG_NUM,
			sizeof(struct mpi_coredump_segment_header) +
			sizeof(mpi_coredump->serdes2_xaui_hss_pcs),
			"XAUI2 HSS PCS Registers");

	ql_build_coredump_seg_header(&mpi_coredump->xfi2_an_hdr,
			XFI2_AN_SEG_NUM,
			sizeof(struct mpi_coredump_segment_header) +
			sizeof(mpi_coredump->serdes2_xfi_an),
			"XFI2 AN Registers");

	ql_build_coredump_seg_header(&mpi_coredump->xfi2_train_hdr,
			XFI2_TRAIN_SEG_NUM,
			sizeof(struct mpi_coredump_segment_header) +
			sizeof(mpi_coredump->serdes2_xfi_train),
			"XFI2 TRAIN Registers");

	ql_build_coredump_seg_header(&mpi_coredump->xfi2_hss_pcs_hdr,
			XFI2_HSS_PCS_SEG_NUM,
			sizeof(struct mpi_coredump_segment_header) +
			sizeof(mpi_coredump->serdes2_xfi_hss_pcs),
			"XFI2 HSS PCS Registers");

	ql_build_coredump_seg_header(&mpi_coredump->xfi2_hss_tx_hdr,
			XFI2_HSS_TX_SEG_NUM,
			sizeof(struct mpi_coredump_segment_header) +
			sizeof(mpi_coredump->serdes2_xfi_hss_tx),
			"XFI2 HSS TX Registers");

	ql_build_coredump_seg_header(&mpi_coredump->xfi2_hss_rx_hdr,
			XFI2_HSS_RX_SEG_NUM,
			sizeof(struct mpi_coredump_segment_header) +
			sizeof(mpi_coredump->serdes2_xfi_hss_rx),
			"XFI2 HSS RX Registers");

	ql_build_coredump_seg_header(&mpi_coredump->xfi2_hss_pll_hdr,
			XFI2_HSS_PLL_SEG_NUM,
			sizeof(struct mpi_coredump_segment_header) +
			sizeof(mpi_coredump->serdes2_xfi_hss_pll),
			"XFI2 HSS PLL Registers");

	status = ql_get_serdes_regs(qdev, mpi_coredump);
	if (status) {
		netif_err(qdev, drv, qdev->ndev,
			  "Failed Dump of Serdes Registers. Status = 0x%.08x\n",
			  status);
		goto err;
	}

	ql_build_coredump_seg_header(&mpi_coredump->core_regs_seg_hdr,
				CORE_SEG_NUM,
				sizeof(mpi_coredump->core_regs_seg_hdr) +
				sizeof(mpi_coredump->mpi_core_regs) +
				sizeof(mpi_coredump->mpi_core_sh_regs),
				"Core Registers");

	/* Get the MPI Core Registers */
	status = ql_get_mpi_regs(qdev, &mpi_coredump->mpi_core_regs[0],
				 MPI_CORE_REGS_ADDR, MPI_CORE_REGS_CNT);
	if (status)
		goto err;
	/* Get the 16 MPI shadow registers */
	status = ql_get_mpi_shadow_regs(qdev,
					&mpi_coredump->mpi_core_sh_regs[0]);
	if (status)
		goto err;

	/* Get the Test Logic Registers */
	ql_build_coredump_seg_header(&mpi_coredump->test_logic_regs_seg_hdr,
				TEST_LOGIC_SEG_NUM,
				sizeof(struct mpi_coredump_segment_header)
				+ sizeof(mpi_coredump->test_logic_regs),
				"Test Logic Regs");
	status = ql_get_mpi_regs(qdev, &mpi_coredump->test_logic_regs[0],
				 TEST_REGS_ADDR, TEST_REGS_CNT);
	if (status)
		goto err;

	/* Get the RMII Registers */
	ql_build_coredump_seg_header(&mpi_coredump->rmii_regs_seg_hdr,
				RMII_SEG_NUM,
				sizeof(struct mpi_coredump_segment_header)
				+ sizeof(mpi_coredump->rmii_regs),
				"RMII Registers");
	status = ql_get_mpi_regs(qdev, &mpi_coredump->rmii_regs[0],
				 RMII_REGS_ADDR, RMII_REGS_CNT);
	if (status)
		goto err;

	/* Get the FCMAC1 Registers */
	ql_build_coredump_seg_header(&mpi_coredump->fcmac1_regs_seg_hdr,
				FCMAC1_SEG_NUM,
				sizeof(struct mpi_coredump_segment_header)
				+ sizeof(mpi_coredump->fcmac1_regs),
				"FCMAC1 Registers");
	status = ql_get_mpi_regs(qdev, &mpi_coredump->fcmac1_regs[0],
				 FCMAC1_REGS_ADDR, FCMAC_REGS_CNT);
	if (status)
		goto err;

	/* Get the FCMAC2 Registers */

	ql_build_coredump_seg_header(&mpi_coredump->fcmac2_regs_seg_hdr,
				FCMAC2_SEG_NUM,
				sizeof(struct mpi_coredump_segment_header)
				+ sizeof(mpi_coredump->fcmac2_regs),
				"FCMAC2 Registers");

	status = ql_get_mpi_regs(qdev, &mpi_coredump->fcmac2_regs[0],
				 FCMAC2_REGS_ADDR, FCMAC_REGS_CNT);
	if (status)
		goto err;

	/* Get the FC1 MBX Registers */
	ql_build_coredump_seg_header(&mpi_coredump->fc1_mbx_regs_seg_hdr,
				FC1_MBOX_SEG_NUM,
				sizeof(struct mpi_coredump_segment_header)
				+ sizeof(mpi_coredump->fc1_mbx_regs),
				"FC1 MBox Regs");
	status = ql_get_mpi_regs(qdev, &mpi_coredump->fc1_mbx_regs[0],
				 FC1_MBX_REGS_ADDR, FC_MBX_REGS_CNT);
	if (status)
		goto err;

	/* Get the IDE Registers */
	ql_build_coredump_seg_header(&mpi_coredump->ide_regs_seg_hdr,
				IDE_SEG_NUM,
				sizeof(struct mpi_coredump_segment_header)
				+ sizeof(mpi_coredump->ide_regs),
				"IDE Registers");
	status = ql_get_mpi_regs(qdev, &mpi_coredump->ide_regs[0],
				 IDE_REGS_ADDR, IDE_REGS_CNT);
	if (status)
		goto err;

	/* Get the NIC1 MBX Registers */
	ql_build_coredump_seg_header(&mpi_coredump->nic1_mbx_regs_seg_hdr,
				NIC1_MBOX_SEG_NUM,
				sizeof(struct mpi_coredump_segment_header)
				+ sizeof(mpi_coredump->nic1_mbx_regs),
				"NIC1 MBox Regs");
	status = ql_get_mpi_regs(qdev, &mpi_coredump->nic1_mbx_regs[0],
				 NIC1_MBX_REGS_ADDR, NIC_MBX_REGS_CNT);
	if (status)
		goto err;

	/* Get the SMBus Registers */
	ql_build_coredump_seg_header(&mpi_coredump->smbus_regs_seg_hdr,
				SMBUS_SEG_NUM,
				sizeof(struct mpi_coredump_segment_header)
				+ sizeof(mpi_coredump->smbus_regs),
				"SMBus Registers");
	status = ql_get_mpi_regs(qdev, &mpi_coredump->smbus_regs[0],
				 SMBUS_REGS_ADDR, SMBUS_REGS_CNT);
	if (status)
		goto err;

	/* Get the FC2 MBX Registers */
	ql_build_coredump_seg_header(&mpi_coredump->fc2_mbx_regs_seg_hdr,
				FC2_MBOX_SEG_NUM,
				sizeof(struct mpi_coredump_segment_header)
				+ sizeof(mpi_coredump->fc2_mbx_regs),
				"FC2 MBox Regs");
	status = ql_get_mpi_regs(qdev, &mpi_coredump->fc2_mbx_regs[0],
				 FC2_MBX_REGS_ADDR, FC_MBX_REGS_CNT);
	if (status)
		goto err;

	/* Get the NIC2 MBX Registers */
	ql_build_coredump_seg_header(&mpi_coredump->nic2_mbx_regs_seg_hdr,
				NIC2_MBOX_SEG_NUM,
				sizeof(struct mpi_coredump_segment_header)
				+ sizeof(mpi_coredump->nic2_mbx_regs),
				"NIC2 MBox Regs");
	status = ql_get_mpi_regs(qdev, &mpi_coredump->nic2_mbx_regs[0],
				 NIC2_MBX_REGS_ADDR, NIC_MBX_REGS_CNT);
	if (status)
		goto err;

	/* Get the I2C Registers */
	ql_build_coredump_seg_header(&mpi_coredump->i2c_regs_seg_hdr,
				I2C_SEG_NUM,
				sizeof(struct mpi_coredump_segment_header)
				+ sizeof(mpi_coredump->i2c_regs),
				"I2C Registers");
	status = ql_get_mpi_regs(qdev, &mpi_coredump->i2c_regs[0],
				 I2C_REGS_ADDR, I2C_REGS_CNT);
	if (status)
		goto err;

	/* Get the MEMC Registers */
	ql_build_coredump_seg_header(&mpi_coredump->memc_regs_seg_hdr,
				MEMC_SEG_NUM,
				sizeof(struct mpi_coredump_segment_header)
				+ sizeof(mpi_coredump->memc_regs),
				"MEMC Registers");
	status = ql_get_mpi_regs(qdev, &mpi_coredump->memc_regs[0],
				 MEMC_REGS_ADDR, MEMC_REGS_CNT);
	if (status)
		goto err;

	/* Get the PBus Registers */
	ql_build_coredump_seg_header(&mpi_coredump->pbus_regs_seg_hdr,
				PBUS_SEG_NUM,
				sizeof(struct mpi_coredump_segment_header)
				+ sizeof(mpi_coredump->pbus_regs),
				"PBUS Registers");
	status = ql_get_mpi_regs(qdev, &mpi_coredump->pbus_regs[0],
				 PBUS_REGS_ADDR, PBUS_REGS_CNT);
	if (status)
		goto err;

	/* Get the MDE Registers */
	ql_build_coredump_seg_header(&mpi_coredump->mde_regs_seg_hdr,
				MDE_SEG_NUM,
				sizeof(struct mpi_coredump_segment_header)
				+ sizeof(mpi_coredump->mde_regs),
				"MDE Registers");
	status = ql_get_mpi_regs(qdev, &mpi_coredump->mde_regs[0],
				 MDE_REGS_ADDR, MDE_REGS_CNT);
	if (status)
		goto err;

	ql_build_coredump_seg_header(&mpi_coredump->misc_nic_seg_hdr,
				MISC_NIC_INFO_SEG_NUM,
				sizeof(struct mpi_coredump_segment_header)
				+ sizeof(mpi_coredump->misc_nic_info),
				"MISC NIC INFO");
	mpi_coredump->misc_nic_info.rx_ring_count = qdev->rx_ring_count;
	mpi_coredump->misc_nic_info.tx_ring_count = qdev->tx_ring_count;
	mpi_coredump->misc_nic_info.intr_count = qdev->intr_count;
	mpi_coredump->misc_nic_info.function = qdev->func;

	/* Segment 31 */
	/* Get indexed register values. */
	ql_build_coredump_seg_header(&mpi_coredump->intr_states_seg_hdr,
				INTR_STATES_SEG_NUM,
				sizeof(struct mpi_coredump_segment_header)
				+ sizeof(mpi_coredump->intr_states),
				"INTR States");
	ql_get_intr_states(qdev, &mpi_coredump->intr_states[0]);

	ql_build_coredump_seg_header(&mpi_coredump->cam_entries_seg_hdr,
				CAM_ENTRIES_SEG_NUM,
				sizeof(struct mpi_coredump_segment_header)
				+ sizeof(mpi_coredump->cam_entries),
				"CAM Entries");
	status = ql_get_cam_entries(qdev, &mpi_coredump->cam_entries[0]);
	if (status)
		goto err;

	ql_build_coredump_seg_header(&mpi_coredump->nic_routing_words_seg_hdr,
				ROUTING_WORDS_SEG_NUM,
				sizeof(struct mpi_coredump_segment_header)
				+ sizeof(mpi_coredump->nic_routing_words),
				"Routing Words");
	status = ql_get_routing_entries(qdev,
			 &mpi_coredump->nic_routing_words[0]);
	if (status)
		goto err;

	/* Segment 34 (Rev C. step 23) */
	ql_build_coredump_seg_header(&mpi_coredump->ets_seg_hdr,
				ETS_SEG_NUM,
				sizeof(struct mpi_coredump_segment_header)
				+ sizeof(mpi_coredump->ets),
				"ETS Registers");
	status = ql_get_ets_regs(qdev, &mpi_coredump->ets[0]);
	if (status)
		goto err;

	ql_build_coredump_seg_header(&mpi_coredump->probe_dump_seg_hdr,
				PROBE_DUMP_SEG_NUM,
				sizeof(struct mpi_coredump_segment_header)
				+ sizeof(mpi_coredump->probe_dump),
				"Probe Dump");
	ql_get_probe_dump(qdev, &mpi_coredump->probe_dump[0]);

	ql_build_coredump_seg_header(&mpi_coredump->routing_reg_seg_hdr,
				ROUTING_INDEX_SEG_NUM,
				sizeof(struct mpi_coredump_segment_header)
				+ sizeof(mpi_coredump->routing_regs),
				"Routing Regs");
	status = ql_get_routing_index_registers(qdev,
					&mpi_coredump->routing_regs[0]);
	if (status)
		goto err;

	ql_build_coredump_seg_header(&mpi_coredump->mac_prot_reg_seg_hdr,
				MAC_PROTOCOL_SEG_NUM,
				sizeof(struct mpi_coredump_segment_header)
				+ sizeof(mpi_coredump->mac_prot_regs),
				"MAC Prot Regs");
	ql_get_mac_protocol_registers(qdev, &mpi_coredump->mac_prot_regs[0]);

	/* Get the semaphore registers for all 5 functions */
	ql_build_coredump_seg_header(&mpi_coredump->sem_regs_seg_hdr,
			SEM_REGS_SEG_NUM,
			sizeof(struct mpi_coredump_segment_header) +
			sizeof(mpi_coredump->sem_regs),	"Sem Registers");

	ql_get_sem_registers(qdev, &mpi_coredump->sem_regs[0]);

	/* Prevent the mpi restarting while we dump the memory.*/
	ql_write_mpi_reg(qdev, MPI_TEST_FUNC_RST_STS, MPI_TEST_FUNC_RST_FRC);

	/* clear the pause */
	status = ql_unpause_mpi_risc(qdev);
	if (status) {
		netif_err(qdev, drv, qdev->ndev,
			  "Failed RISC unpause. Status = 0x%.08x\n", status);
		goto err;
	}

	/* Reset the RISC so we can dump RAM */
	status = ql_hard_reset_mpi_risc(qdev);
	if (status) {
		netif_err(qdev, drv, qdev->ndev,
			  "Failed RISC reset. Status = 0x%.08x\n", status);
		goto err;
	}

	ql_build_coredump_seg_header(&mpi_coredump->code_ram_seg_hdr,
				WCS_RAM_SEG_NUM,
				sizeof(struct mpi_coredump_segment_header)
				+ sizeof(mpi_coredump->code_ram),
				"WCS RAM");
	status = ql_dump_risc_ram_area(qdev, &mpi_coredump->code_ram[0],
					CODE_RAM_ADDR, CODE_RAM_CNT);
	if (status) {
		netif_err(qdev, drv, qdev->ndev,
			  "Failed Dump of CODE RAM. Status = 0x%.08x\n",
			  status);
		goto err;
	}

	/* Insert the segment header */
	ql_build_coredump_seg_header(&mpi_coredump->memc_ram_seg_hdr,
				MEMC_RAM_SEG_NUM,
				sizeof(struct mpi_coredump_segment_header)
				+ sizeof(mpi_coredump->memc_ram),
				"MEMC RAM");
	status = ql_dump_risc_ram_area(qdev, &mpi_coredump->memc_ram[0],
					MEMC_RAM_ADDR, MEMC_RAM_CNT);
	if (status) {
		netif_err(qdev, drv, qdev->ndev,
			  "Failed Dump of MEMC RAM. Status = 0x%.08x\n",
			  status);
		goto err;
	}
err:
	ql_sem_unlock(qdev, SEM_PROC_REG_MASK); /* does flush too */
	return status;

}

static void ql_get_core_dump(struct ql_adapter *qdev)
{
	if (!ql_own_firmware(qdev)) {
		netif_err(qdev, drv, qdev->ndev, "Don't own firmware!\n");
		return;
	}

	if (!netif_running(qdev->ndev)) {
		netif_err(qdev, ifup, qdev->ndev,
			  "Force Coredump can only be done from interface that is up.\n");
		return;
	}

	if (ql_mb_sys_err(qdev)) {
		netif_err(qdev, ifup, qdev->ndev,
			  "Fail force coredump with ql_mb_sys_err().\n");
		return;
	}
}

void ql_gen_reg_dump(struct ql_adapter *qdev,
			struct ql_reg_dump *mpi_coredump)
{
	int i, status;


	memset(&(mpi_coredump->mpi_global_header), 0,
		sizeof(struct mpi_coredump_global_header));
	mpi_coredump->mpi_global_header.cookie = MPI_COREDUMP_COOKIE;
	mpi_coredump->mpi_global_header.headerSize =
		sizeof(struct mpi_coredump_global_header);
	mpi_coredump->mpi_global_header.imageSize =
		sizeof(struct ql_reg_dump);
	memcpy(mpi_coredump->mpi_global_header.idString, "MPI Coredump",
		sizeof(mpi_coredump->mpi_global_header.idString));


	/* segment 16 */
	ql_build_coredump_seg_header(&mpi_coredump->misc_nic_seg_hdr,
				MISC_NIC_INFO_SEG_NUM,
				sizeof(struct mpi_coredump_segment_header)
				+ sizeof(mpi_coredump->misc_nic_info),
				"MISC NIC INFO");
	mpi_coredump->misc_nic_info.rx_ring_count = qdev->rx_ring_count;
	mpi_coredump->misc_nic_info.tx_ring_count = qdev->tx_ring_count;
	mpi_coredump->misc_nic_info.intr_count = qdev->intr_count;
	mpi_coredump->misc_nic_info.function = qdev->func;

	/* Segment 16, Rev C. Step 18 */
	ql_build_coredump_seg_header(&mpi_coredump->nic_regs_seg_hdr,
				NIC1_CONTROL_SEG_NUM,
				sizeof(struct mpi_coredump_segment_header)
				+ sizeof(mpi_coredump->nic_regs),
				"NIC Registers");
	/* Get generic reg dump */
	for (i = 0; i < 64; i++)
		mpi_coredump->nic_regs[i] = ql_read32(qdev, i * sizeof(u32));

	/* Segment 31 */
	/* Get indexed register values. */
	ql_build_coredump_seg_header(&mpi_coredump->intr_states_seg_hdr,
				INTR_STATES_SEG_NUM,
				sizeof(struct mpi_coredump_segment_header)
				+ sizeof(mpi_coredump->intr_states),
				"INTR States");
	ql_get_intr_states(qdev, &mpi_coredump->intr_states[0]);

	ql_build_coredump_seg_header(&mpi_coredump->cam_entries_seg_hdr,
				CAM_ENTRIES_SEG_NUM,
				sizeof(struct mpi_coredump_segment_header)
				+ sizeof(mpi_coredump->cam_entries),
				"CAM Entries");
	status = ql_get_cam_entries(qdev, &mpi_coredump->cam_entries[0]);
	if (status)
		return;

	ql_build_coredump_seg_header(&mpi_coredump->nic_routing_words_seg_hdr,
				ROUTING_WORDS_SEG_NUM,
				sizeof(struct mpi_coredump_segment_header)
				+ sizeof(mpi_coredump->nic_routing_words),
				"Routing Words");
	status = ql_get_routing_entries(qdev,
			 &mpi_coredump->nic_routing_words[0]);
	if (status)
		return;

	/* Segment 34 (Rev C. step 23) */
	ql_build_coredump_seg_header(&mpi_coredump->ets_seg_hdr,
				ETS_SEG_NUM,
				sizeof(struct mpi_coredump_segment_header)
				+ sizeof(mpi_coredump->ets),
				"ETS Registers");
	status = ql_get_ets_regs(qdev, &mpi_coredump->ets[0]);
	if (status)
		return;

	if (test_bit(QL_FRC_COREDUMP, &qdev->flags))
		ql_get_core_dump(qdev);
}

/* Coredump to messages log file using separate worker thread */
void ql_mpi_core_to_log(struct work_struct *work)
{
	struct ql_adapter *qdev =
		container_of(work, struct ql_adapter, mpi_core_to_log.work);
	u32 *tmp, count;
	int i;

	count = sizeof(struct ql_mpi_coredump) / sizeof(u32);
	tmp = (u32 *)qdev->mpi_coredump;
	netif_printk(qdev, drv, KERN_DEBUG, qdev->ndev,
		     "Core is dumping to log file!\n");

	for (i = 0; i < count; i += 8) {
		printk(KERN_ERR "%.08x: %.08x %.08x %.08x %.08x %.08x "
			"%.08x %.08x %.08x\n", i,
			tmp[i + 0],
			tmp[i + 1],
			tmp[i + 2],
			tmp[i + 3],
			tmp[i + 4],
			tmp[i + 5],
			tmp[i + 6],
			tmp[i + 7]);
		msleep(5);
	}
}

#ifdef QL_REG_DUMP
static void ql_dump_intr_states(struct ql_adapter *qdev)
{
	int i;
	u32 value;
	for (i = 0; i < qdev->intr_count; i++) {
		ql_write32(qdev, INTR_EN, qdev->intr_context[i].intr_read_mask);
		value = ql_read32(qdev, INTR_EN);
		printk(KERN_ERR PFX
		       "%s: Interrupt %d is %s.\n",
		       qdev->ndev->name, i,
		       (value & INTR_EN_EN ? "enabled" : "disabled"));
	}
}

void ql_dump_xgmac_control_regs(struct ql_adapter *qdev)
{
	u32 data;
	if (ql_sem_spinlock(qdev, qdev->xg_sem_mask)) {
		printk(KERN_ERR "%s: Couldn't get xgmac sem.\n", __func__);
		return;
	}
	ql_read_xgmac_reg(qdev, PAUSE_SRC_LO, &data);
	printk(KERN_ERR PFX "%s: PAUSE_SRC_LO = 0x%.08x.\n", qdev->ndev->name,
	       data);
	ql_read_xgmac_reg(qdev, PAUSE_SRC_HI, &data);
	printk(KERN_ERR PFX "%s: PAUSE_SRC_HI = 0x%.08x.\n", qdev->ndev->name,
	       data);
	ql_read_xgmac_reg(qdev, GLOBAL_CFG, &data);
	printk(KERN_ERR PFX "%s: GLOBAL_CFG = 0x%.08x.\n", qdev->ndev->name,
	       data);
	ql_read_xgmac_reg(qdev, TX_CFG, &data);
	printk(KERN_ERR PFX "%s: TX_CFG = 0x%.08x.\n", qdev->ndev->name, data);
	ql_read_xgmac_reg(qdev, RX_CFG, &data);
	printk(KERN_ERR PFX "%s: RX_CFG = 0x%.08x.\n", qdev->ndev->name, data);
	ql_read_xgmac_reg(qdev, FLOW_CTL, &data);
	printk(KERN_ERR PFX "%s: FLOW_CTL = 0x%.08x.\n", qdev->ndev->name,
	       data);
	ql_read_xgmac_reg(qdev, PAUSE_OPCODE, &data);
	printk(KERN_ERR PFX "%s: PAUSE_OPCODE = 0x%.08x.\n", qdev->ndev->name,
	       data);
	ql_read_xgmac_reg(qdev, PAUSE_TIMER, &data);
	printk(KERN_ERR PFX "%s: PAUSE_TIMER = 0x%.08x.\n", qdev->ndev->name,
	       data);
	ql_read_xgmac_reg(qdev, PAUSE_FRM_DEST_LO, &data);
	printk(KERN_ERR PFX "%s: PAUSE_FRM_DEST_LO = 0x%.08x.\n",
	       qdev->ndev->name, data);
	ql_read_xgmac_reg(qdev, PAUSE_FRM_DEST_HI, &data);
	printk(KERN_ERR PFX "%s: PAUSE_FRM_DEST_HI = 0x%.08x.\n",
	       qdev->ndev->name, data);
	ql_read_xgmac_reg(qdev, MAC_TX_PARAMS, &data);
	printk(KERN_ERR PFX "%s: MAC_TX_PARAMS = 0x%.08x.\n", qdev->ndev->name,
	       data);
	ql_read_xgmac_reg(qdev, MAC_RX_PARAMS, &data);
	printk(KERN_ERR PFX "%s: MAC_RX_PARAMS = 0x%.08x.\n", qdev->ndev->name,
	       data);
	ql_read_xgmac_reg(qdev, MAC_SYS_INT, &data);
	printk(KERN_ERR PFX "%s: MAC_SYS_INT = 0x%.08x.\n", qdev->ndev->name,
	       data);
	ql_read_xgmac_reg(qdev, MAC_SYS_INT_MASK, &data);
	printk(KERN_ERR PFX "%s: MAC_SYS_INT_MASK = 0x%.08x.\n",
	       qdev->ndev->name, data);
	ql_read_xgmac_reg(qdev, MAC_MGMT_INT, &data);
	printk(KERN_ERR PFX "%s: MAC_MGMT_INT = 0x%.08x.\n", qdev->ndev->name,
	       data);
	ql_read_xgmac_reg(qdev, MAC_MGMT_IN_MASK, &data);
	printk(KERN_ERR PFX "%s: MAC_MGMT_IN_MASK = 0x%.08x.\n",
	       qdev->ndev->name, data);
	ql_read_xgmac_reg(qdev, EXT_ARB_MODE, &data);
	printk(KERN_ERR PFX "%s: EXT_ARB_MODE = 0x%.08x.\n", qdev->ndev->name,
	       data);
	ql_sem_unlock(qdev, qdev->xg_sem_mask);

}

static void ql_dump_ets_regs(struct ql_adapter *qdev)
{
}

static void ql_dump_cam_entries(struct ql_adapter *qdev)
{
	int i;
	u32 value[3];

	i = ql_sem_spinlock(qdev, SEM_MAC_ADDR_MASK);
	if (i)
		return;
	for (i = 0; i < 4; i++) {
		if (ql_get_mac_addr_reg(qdev, MAC_ADDR_TYPE_CAM_MAC, i, value)) {
			printk(KERN_ERR PFX
			       "%s: Failed read of mac index register.\n",
			       __func__);
			return;
		} else {
			if (value[0])
				printk(KERN_ERR PFX
				       "%s: CAM index %d CAM Lookup Lower = 0x%.08x:%.08x, Output = 0x%.08x.\n",
				       qdev->ndev->name, i, value[1], value[0],
				       value[2]);
		}
	}
	for (i = 0; i < 32; i++) {
		if (ql_get_mac_addr_reg
		    (qdev, MAC_ADDR_TYPE_MULTI_MAC, i, value)) {
			printk(KERN_ERR PFX
			       "%s: Failed read of mac index register.\n",
			       __func__);
			return;
		} else {
			if (value[0])
				printk(KERN_ERR PFX
				       "%s: MCAST index %d CAM Lookup Lower = 0x%.08x:%.08x.\n",
				       qdev->ndev->name, i, value[1], value[0]);
		}
	}
	ql_sem_unlock(qdev, SEM_MAC_ADDR_MASK);
}

void ql_dump_routing_entries(struct ql_adapter *qdev)
{
	int i;
	u32 value;
	i = ql_sem_spinlock(qdev, SEM_RT_IDX_MASK);
	if (i)
		return;
	for (i = 0; i < 16; i++) {
		value = 0;
		if (ql_get_routing_reg(qdev, i, &value)) {
			printk(KERN_ERR PFX
			       "%s: Failed read of routing index register.\n",
			       __func__);
			return;
		} else {
			if (value)
				printk(KERN_ERR PFX
				       "%s: Routing Mask %d = 0x%.08x.\n",
				       qdev->ndev->name, i, value);
		}
	}
	ql_sem_unlock(qdev, SEM_RT_IDX_MASK);
}

void ql_dump_regs(struct ql_adapter *qdev)
{
	printk(KERN_ERR PFX "reg dump for function #%d.\n", qdev->func);
	printk(KERN_ERR PFX "SYS	 			= 0x%x.\n",
	       ql_read32(qdev, SYS));
	printk(KERN_ERR PFX "RST_FO 			= 0x%x.\n",
	       ql_read32(qdev, RST_FO));
	printk(KERN_ERR PFX "FSC 				= 0x%x.\n",
	       ql_read32(qdev, FSC));
	printk(KERN_ERR PFX "CSR 				= 0x%x.\n",
	       ql_read32(qdev, CSR));
	printk(KERN_ERR PFX "ICB_RID 			= 0x%x.\n",
	       ql_read32(qdev, ICB_RID));
	printk(KERN_ERR PFX "ICB_L 				= 0x%x.\n",
	       ql_read32(qdev, ICB_L));
	printk(KERN_ERR PFX "ICB_H 				= 0x%x.\n",
	       ql_read32(qdev, ICB_H));
	printk(KERN_ERR PFX "CFG 				= 0x%x.\n",
	       ql_read32(qdev, CFG));
	printk(KERN_ERR PFX "BIOS_ADDR 			= 0x%x.\n",
	       ql_read32(qdev, BIOS_ADDR));
	printk(KERN_ERR PFX "STS 				= 0x%x.\n",
	       ql_read32(qdev, STS));
	printk(KERN_ERR PFX "INTR_EN			= 0x%x.\n",
	       ql_read32(qdev, INTR_EN));
	printk(KERN_ERR PFX "INTR_MASK 			= 0x%x.\n",
	       ql_read32(qdev, INTR_MASK));
	printk(KERN_ERR PFX "ISR1 				= 0x%x.\n",
	       ql_read32(qdev, ISR1));
	printk(KERN_ERR PFX "ISR2 				= 0x%x.\n",
	       ql_read32(qdev, ISR2));
	printk(KERN_ERR PFX "ISR3 				= 0x%x.\n",
	       ql_read32(qdev, ISR3));
	printk(KERN_ERR PFX "ISR4 				= 0x%x.\n",
	       ql_read32(qdev, ISR4));
	printk(KERN_ERR PFX "REV_ID 			= 0x%x.\n",
	       ql_read32(qdev, REV_ID));
	printk(KERN_ERR PFX "FRC_ECC_ERR 			= 0x%x.\n",
	       ql_read32(qdev, FRC_ECC_ERR));
	printk(KERN_ERR PFX "ERR_STS 			= 0x%x.\n",
	       ql_read32(qdev, ERR_STS));
	printk(KERN_ERR PFX "RAM_DBG_ADDR 			= 0x%x.\n",
	       ql_read32(qdev, RAM_DBG_ADDR));
	printk(KERN_ERR PFX "RAM_DBG_DATA 			= 0x%x.\n",
	       ql_read32(qdev, RAM_DBG_DATA));
	printk(KERN_ERR PFX "ECC_ERR_CNT 			= 0x%x.\n",
	       ql_read32(qdev, ECC_ERR_CNT));
	printk(KERN_ERR PFX "SEM 				= 0x%x.\n",
	       ql_read32(qdev, SEM));
	printk(KERN_ERR PFX "GPIO_1 			= 0x%x.\n",
	       ql_read32(qdev, GPIO_1));
	printk(KERN_ERR PFX "GPIO_2 			= 0x%x.\n",
	       ql_read32(qdev, GPIO_2));
	printk(KERN_ERR PFX "GPIO_3 			= 0x%x.\n",
	       ql_read32(qdev, GPIO_3));
	printk(KERN_ERR PFX "XGMAC_ADDR 			= 0x%x.\n",
	       ql_read32(qdev, XGMAC_ADDR));
	printk(KERN_ERR PFX "XGMAC_DATA 			= 0x%x.\n",
	       ql_read32(qdev, XGMAC_DATA));
	printk(KERN_ERR PFX "NIC_ETS 			= 0x%x.\n",
	       ql_read32(qdev, NIC_ETS));
	printk(KERN_ERR PFX "CNA_ETS 			= 0x%x.\n",
	       ql_read32(qdev, CNA_ETS));
	printk(KERN_ERR PFX "FLASH_ADDR 			= 0x%x.\n",
	       ql_read32(qdev, FLASH_ADDR));
	printk(KERN_ERR PFX "FLASH_DATA 			= 0x%x.\n",
	       ql_read32(qdev, FLASH_DATA));
	printk(KERN_ERR PFX "CQ_STOP 			= 0x%x.\n",
	       ql_read32(qdev, CQ_STOP));
	printk(KERN_ERR PFX "PAGE_TBL_RID 			= 0x%x.\n",
	       ql_read32(qdev, PAGE_TBL_RID));
	printk(KERN_ERR PFX "WQ_PAGE_TBL_LO 		= 0x%x.\n",
	       ql_read32(qdev, WQ_PAGE_TBL_LO));
	printk(KERN_ERR PFX "WQ_PAGE_TBL_HI 		= 0x%x.\n",
	       ql_read32(qdev, WQ_PAGE_TBL_HI));
	printk(KERN_ERR PFX "CQ_PAGE_TBL_LO 		= 0x%x.\n",
	       ql_read32(qdev, CQ_PAGE_TBL_LO));
	printk(KERN_ERR PFX "CQ_PAGE_TBL_HI 		= 0x%x.\n",
	       ql_read32(qdev, CQ_PAGE_TBL_HI));
	printk(KERN_ERR PFX "COS_DFLT_CQ1 			= 0x%x.\n",
	       ql_read32(qdev, COS_DFLT_CQ1));
	printk(KERN_ERR PFX "COS_DFLT_CQ2 			= 0x%x.\n",
	       ql_read32(qdev, COS_DFLT_CQ2));
	printk(KERN_ERR PFX "SPLT_HDR 			= 0x%x.\n",
	       ql_read32(qdev, SPLT_HDR));
	printk(KERN_ERR PFX "FC_PAUSE_THRES 		= 0x%x.\n",
	       ql_read32(qdev, FC_PAUSE_THRES));
	printk(KERN_ERR PFX "NIC_PAUSE_THRES 		= 0x%x.\n",
	       ql_read32(qdev, NIC_PAUSE_THRES));
	printk(KERN_ERR PFX "FC_ETHERTYPE 			= 0x%x.\n",
	       ql_read32(qdev, FC_ETHERTYPE));
	printk(KERN_ERR PFX "FC_RCV_CFG 			= 0x%x.\n",
	       ql_read32(qdev, FC_RCV_CFG));
	printk(KERN_ERR PFX "NIC_RCV_CFG 			= 0x%x.\n",
	       ql_read32(qdev, NIC_RCV_CFG));
	printk(KERN_ERR PFX "FC_COS_TAGS 			= 0x%x.\n",
	       ql_read32(qdev, FC_COS_TAGS));
	printk(KERN_ERR PFX "NIC_COS_TAGS 			= 0x%x.\n",
	       ql_read32(qdev, NIC_COS_TAGS));
	printk(KERN_ERR PFX "MGMT_RCV_CFG 			= 0x%x.\n",
	       ql_read32(qdev, MGMT_RCV_CFG));
	printk(KERN_ERR PFX "XG_SERDES_ADDR 		= 0x%x.\n",
	       ql_read32(qdev, XG_SERDES_ADDR));
	printk(KERN_ERR PFX "XG_SERDES_DATA 		= 0x%x.\n",
	       ql_read32(qdev, XG_SERDES_DATA));
	printk(KERN_ERR PFX "PRB_MX_ADDR 			= 0x%x.\n",
	       ql_read32(qdev, PRB_MX_ADDR));
	printk(KERN_ERR PFX "PRB_MX_DATA 			= 0x%x.\n",
	       ql_read32(qdev, PRB_MX_DATA));
	ql_dump_intr_states(qdev);
	ql_dump_xgmac_control_regs(qdev);
	ql_dump_ets_regs(qdev);
	ql_dump_cam_entries(qdev);
	ql_dump_routing_entries(qdev);
}
#endif

#ifdef QL_STAT_DUMP
void ql_dump_stat(struct ql_adapter *qdev)
{
	printk(KERN_ERR "%s: Enter.\n", __func__);
	printk(KERN_ERR "tx_pkts = %ld\n",
	       (unsigned long)qdev->nic_stats.tx_pkts);
	printk(KERN_ERR "tx_bytes = %ld\n",
	       (unsigned long)qdev->nic_stats.tx_bytes);
	printk(KERN_ERR "tx_mcast_pkts = %ld.\n",
	       (unsigned long)qdev->nic_stats.tx_mcast_pkts);
	printk(KERN_ERR "tx_bcast_pkts = %ld.\n",
	       (unsigned long)qdev->nic_stats.tx_bcast_pkts);
	printk(KERN_ERR "tx_ucast_pkts = %ld.\n",
	       (unsigned long)qdev->nic_stats.tx_ucast_pkts);
	printk(KERN_ERR "tx_ctl_pkts = %ld.\n",
	       (unsigned long)qdev->nic_stats.tx_ctl_pkts);
	printk(KERN_ERR "tx_pause_pkts = %ld.\n",
	       (unsigned long)qdev->nic_stats.tx_pause_pkts);
	printk(KERN_ERR "tx_64_pkt = %ld.\n",
	       (unsigned long)qdev->nic_stats.tx_64_pkt);
	printk(KERN_ERR "tx_65_to_127_pkt = %ld.\n",
	       (unsigned long)qdev->nic_stats.tx_65_to_127_pkt);
	printk(KERN_ERR "tx_128_to_255_pkt = %ld.\n",
	       (unsigned long)qdev->nic_stats.tx_128_to_255_pkt);
	printk(KERN_ERR "tx_256_511_pkt = %ld.\n",
	       (unsigned long)qdev->nic_stats.tx_256_511_pkt);
	printk(KERN_ERR "tx_512_to_1023_pkt = %ld.\n",
	       (unsigned long)qdev->nic_stats.tx_512_to_1023_pkt);
	printk(KERN_ERR "tx_1024_to_1518_pkt = %ld.\n",
	       (unsigned long)qdev->nic_stats.tx_1024_to_1518_pkt);
	printk(KERN_ERR "tx_1519_to_max_pkt = %ld.\n",
	       (unsigned long)qdev->nic_stats.tx_1519_to_max_pkt);
	printk(KERN_ERR "tx_undersize_pkt = %ld.\n",
	       (unsigned long)qdev->nic_stats.tx_undersize_pkt);
	printk(KERN_ERR "tx_oversize_pkt = %ld.\n",
	       (unsigned long)qdev->nic_stats.tx_oversize_pkt);
	printk(KERN_ERR "rx_bytes = %ld.\n",
	       (unsigned long)qdev->nic_stats.rx_bytes);
	printk(KERN_ERR "rx_bytes_ok = %ld.\n",
	       (unsigned long)qdev->nic_stats.rx_bytes_ok);
	printk(KERN_ERR "rx_pkts = %ld.\n",
	       (unsigned long)qdev->nic_stats.rx_pkts);
	printk(KERN_ERR "rx_pkts_ok = %ld.\n",
	       (unsigned long)qdev->nic_stats.rx_pkts_ok);
	printk(KERN_ERR "rx_bcast_pkts = %ld.\n",
	       (unsigned long)qdev->nic_stats.rx_bcast_pkts);
	printk(KERN_ERR "rx_mcast_pkts = %ld.\n",
	       (unsigned long)qdev->nic_stats.rx_mcast_pkts);
	printk(KERN_ERR "rx_ucast_pkts = %ld.\n",
	       (unsigned long)qdev->nic_stats.rx_ucast_pkts);
	printk(KERN_ERR "rx_undersize_pkts = %ld.\n",
	       (unsigned long)qdev->nic_stats.rx_undersize_pkts);
	printk(KERN_ERR "rx_oversize_pkts = %ld.\n",
	       (unsigned long)qdev->nic_stats.rx_oversize_pkts);
	printk(KERN_ERR "rx_jabber_pkts = %ld.\n",
	       (unsigned long)qdev->nic_stats.rx_jabber_pkts);
	printk(KERN_ERR "rx_undersize_fcerr_pkts = %ld.\n",
	       (unsigned long)qdev->nic_stats.rx_undersize_fcerr_pkts);
	printk(KERN_ERR "rx_drop_events = %ld.\n",
	       (unsigned long)qdev->nic_stats.rx_drop_events);
	printk(KERN_ERR "rx_fcerr_pkts = %ld.\n",
	       (unsigned long)qdev->nic_stats.rx_fcerr_pkts);
	printk(KERN_ERR "rx_align_err = %ld.\n",
	       (unsigned long)qdev->nic_stats.rx_align_err);
	printk(KERN_ERR "rx_symbol_err = %ld.\n",
	       (unsigned long)qdev->nic_stats.rx_symbol_err);
	printk(KERN_ERR "rx_mac_err = %ld.\n",
	       (unsigned long)qdev->nic_stats.rx_mac_err);
	printk(KERN_ERR "rx_ctl_pkts = %ld.\n",
	       (unsigned long)qdev->nic_stats.rx_ctl_pkts);
	printk(KERN_ERR "rx_pause_pkts = %ld.\n",
	       (unsigned long)qdev->nic_stats.rx_pause_pkts);
	printk(KERN_ERR "rx_64_pkts = %ld.\n",
	       (unsigned long)qdev->nic_stats.rx_64_pkts);
	printk(KERN_ERR "rx_65_to_127_pkts = %ld.\n",
	       (unsigned long)qdev->nic_stats.rx_65_to_127_pkts);
	printk(KERN_ERR "rx_128_255_pkts = %ld.\n",
	       (unsigned long)qdev->nic_stats.rx_128_255_pkts);
	printk(KERN_ERR "rx_256_511_pkts = %ld.\n",
	       (unsigned long)qdev->nic_stats.rx_256_511_pkts);
	printk(KERN_ERR "rx_512_to_1023_pkts = %ld.\n",
	       (unsigned long)qdev->nic_stats.rx_512_to_1023_pkts);
	printk(KERN_ERR "rx_1024_to_1518_pkts = %ld.\n",
	       (unsigned long)qdev->nic_stats.rx_1024_to_1518_pkts);
	printk(KERN_ERR "rx_1519_to_max_pkts = %ld.\n",
	       (unsigned long)qdev->nic_stats.rx_1519_to_max_pkts);
	printk(KERN_ERR "rx_len_err_pkts = %ld.\n",
	       (unsigned long)qdev->nic_stats.rx_len_err_pkts);
};
#endif

#ifdef QL_DEV_DUMP
void ql_dump_qdev(struct ql_adapter *qdev)
{
	int i;
	printk(KERN_ERR PFX "qdev->flags 			= %lx.\n",
	       qdev->flags);
	printk(KERN_ERR PFX "qdev->vlgrp 			= %p.\n",
	       qdev->vlgrp);
	printk(KERN_ERR PFX "qdev->pdev 			= %p.\n",
	       qdev->pdev);
	printk(KERN_ERR PFX "qdev->ndev 			= %p.\n",
	       qdev->ndev);
	printk(KERN_ERR PFX "qdev->chip_rev_id 		= %d.\n",
	       qdev->chip_rev_id);
	printk(KERN_ERR PFX "qdev->reg_base 		= %p.\n",
	       qdev->reg_base);
	printk(KERN_ERR PFX "qdev->doorbell_area 	= %p.\n",
	       qdev->doorbell_area);
	printk(KERN_ERR PFX "qdev->doorbell_area_size 	= %d.\n",
	       qdev->doorbell_area_size);
	printk(KERN_ERR PFX "msg_enable 		= %x.\n",
	       qdev->msg_enable);
	printk(KERN_ERR PFX "qdev->rx_ring_shadow_reg_area	= %p.\n",
	       qdev->rx_ring_shadow_reg_area);
	printk(KERN_ERR PFX "qdev->rx_ring_shadow_reg_dma 	= %llx.\n",
	       (unsigned long long) qdev->rx_ring_shadow_reg_dma);
	printk(KERN_ERR PFX "qdev->tx_ring_shadow_reg_area	= %p.\n",
	       qdev->tx_ring_shadow_reg_area);
	printk(KERN_ERR PFX "qdev->tx_ring_shadow_reg_dma	= %llx.\n",
	       (unsigned long long) qdev->tx_ring_shadow_reg_dma);
	printk(KERN_ERR PFX "qdev->intr_count 		= %d.\n",
	       qdev->intr_count);
	if (qdev->msi_x_entry)
		for (i = 0; i < qdev->intr_count; i++) {
			printk(KERN_ERR PFX
			       "msi_x_entry.[%d]vector	= %d.\n", i,
			       qdev->msi_x_entry[i].vector);
			printk(KERN_ERR PFX
			       "msi_x_entry.[%d]entry	= %d.\n", i,
			       qdev->msi_x_entry[i].entry);
		}
	for (i = 0; i < qdev->intr_count; i++) {
		printk(KERN_ERR PFX
		       "intr_context[%d].qdev		= %p.\n", i,
		       qdev->intr_context[i].qdev);
		printk(KERN_ERR PFX
		       "intr_context[%d].intr		= %d.\n", i,
		       qdev->intr_context[i].intr);
		printk(KERN_ERR PFX
		       "intr_context[%d].hooked		= %d.\n", i,
		       qdev->intr_context[i].hooked);
		printk(KERN_ERR PFX
		       "intr_context[%d].intr_en_mask	= 0x%08x.\n", i,
		       qdev->intr_context[i].intr_en_mask);
		printk(KERN_ERR PFX
		       "intr_context[%d].intr_dis_mask	= 0x%08x.\n", i,
		       qdev->intr_context[i].intr_dis_mask);
		printk(KERN_ERR PFX
		       "intr_context[%d].intr_read_mask	= 0x%08x.\n", i,
		       qdev->intr_context[i].intr_read_mask);
	}
	printk(KERN_ERR PFX "qdev->tx_ring_count = %d.\n", qdev->tx_ring_count);
	printk(KERN_ERR PFX "qdev->rx_ring_count = %d.\n", qdev->rx_ring_count);
	printk(KERN_ERR PFX "qdev->ring_mem_size = %d.\n", qdev->ring_mem_size);
	printk(KERN_ERR PFX "qdev->ring_mem 	= %p.\n", qdev->ring_mem);
	printk(KERN_ERR PFX "qdev->intr_count 	= %d.\n", qdev->intr_count);
	printk(KERN_ERR PFX "qdev->tx_ring		= %p.\n",
	       qdev->tx_ring);
	printk(KERN_ERR PFX "qdev->rss_ring_count 	= %d.\n",
	       qdev->rss_ring_count);
	printk(KERN_ERR PFX "qdev->rx_ring	= %p.\n", qdev->rx_ring);
	printk(KERN_ERR PFX "qdev->default_rx_queue	= %d.\n",
	       qdev->default_rx_queue);
	printk(KERN_ERR PFX "qdev->xg_sem_mask		= 0x%08x.\n",
	       qdev->xg_sem_mask);
	printk(KERN_ERR PFX "qdev->port_link_up		= 0x%08x.\n",
	       qdev->port_link_up);
	printk(KERN_ERR PFX "qdev->port_init		= 0x%08x.\n",
	       qdev->port_init);

}
#endif

#ifdef QL_CB_DUMP
void ql_dump_wqicb(struct wqicb *wqicb)
{
	printk(KERN_ERR PFX "Dumping wqicb stuff...\n");
	printk(KERN_ERR PFX "wqicb->len = 0x%x.\n", le16_to_cpu(wqicb->len));
	printk(KERN_ERR PFX "wqicb->flags = %x.\n", le16_to_cpu(wqicb->flags));
	printk(KERN_ERR PFX "wqicb->cq_id_rss = %d.\n",
	       le16_to_cpu(wqicb->cq_id_rss));
	printk(KERN_ERR PFX "wqicb->rid = 0x%x.\n", le16_to_cpu(wqicb->rid));
	printk(KERN_ERR PFX "wqicb->wq_addr = 0x%llx.\n",
	       (unsigned long long) le64_to_cpu(wqicb->addr));
	printk(KERN_ERR PFX "wqicb->wq_cnsmr_idx_addr = 0x%llx.\n",
	       (unsigned long long) le64_to_cpu(wqicb->cnsmr_idx_addr));
}

void ql_dump_tx_ring(struct tx_ring *tx_ring)
{
	if (tx_ring == NULL)
		return;
	printk(KERN_ERR PFX
	       "===================== Dumping tx_ring %d ===============.\n",
	       tx_ring->wq_id);
	printk(KERN_ERR PFX "tx_ring->base = %p.\n", tx_ring->wq_base);
	printk(KERN_ERR PFX "tx_ring->base_dma = 0x%llx.\n",
	       (unsigned long long) tx_ring->wq_base_dma);
	printk(KERN_ERR PFX
	       "tx_ring->cnsmr_idx_sh_reg, addr = 0x%p, value = %d.\n",
	       tx_ring->cnsmr_idx_sh_reg,
	       tx_ring->cnsmr_idx_sh_reg
			? ql_read_sh_reg(tx_ring->cnsmr_idx_sh_reg) : 0);
	printk(KERN_ERR PFX "tx_ring->size = %d.\n", tx_ring->wq_size);
	printk(KERN_ERR PFX "tx_ring->len = %d.\n", tx_ring->wq_len);
	printk(KERN_ERR PFX "tx_ring->prod_idx_db_reg = %p.\n",
	       tx_ring->prod_idx_db_reg);
	printk(KERN_ERR PFX "tx_ring->valid_db_reg = %p.\n",
	       tx_ring->valid_db_reg);
	printk(KERN_ERR PFX "tx_ring->prod_idx = %d.\n", tx_ring->prod_idx);
	printk(KERN_ERR PFX "tx_ring->cq_id = %d.\n", tx_ring->cq_id);
	printk(KERN_ERR PFX "tx_ring->wq_id = %d.\n", tx_ring->wq_id);
	printk(KERN_ERR PFX "tx_ring->q = %p.\n", tx_ring->q);
	printk(KERN_ERR PFX "tx_ring->tx_count = %d.\n",
	       atomic_read(&tx_ring->tx_count));
}

void ql_dump_ricb(struct ricb *ricb)
{
	int i;
	printk(KERN_ERR PFX
	       "===================== Dumping ricb ===============.\n");
	printk(KERN_ERR PFX "Dumping ricb stuff...\n");

	printk(KERN_ERR PFX "ricb->base_cq = %d.\n", ricb->base_cq & 0x1f);
	printk(KERN_ERR PFX "ricb->flags = %s%s%s%s%s%s%s%s%s.\n",
	       ricb->base_cq & RSS_L4K ? "RSS_L4K " : "",
	       ricb->flags & RSS_L6K ? "RSS_L6K " : "",
	       ricb->flags & RSS_LI ? "RSS_LI " : "",
	       ricb->flags & RSS_LB ? "RSS_LB " : "",
	       ricb->flags & RSS_LM ? "RSS_LM " : "",
	       ricb->flags & RSS_RI4 ? "RSS_RI4 " : "",
	       ricb->flags & RSS_RT4 ? "RSS_RT4 " : "",
	       ricb->flags & RSS_RI6 ? "RSS_RI6 " : "",
	       ricb->flags & RSS_RT6 ? "RSS_RT6 " : "");
	printk(KERN_ERR PFX "ricb->mask = 0x%.04x.\n", le16_to_cpu(ricb->mask));
	for (i = 0; i < 16; i++)
		printk(KERN_ERR PFX "ricb->hash_cq_id[%d] = 0x%.08x.\n", i,
		       le32_to_cpu(ricb->hash_cq_id[i]));
	for (i = 0; i < 10; i++)
		printk(KERN_ERR PFX "ricb->ipv6_hash_key[%d] = 0x%.08x.\n", i,
		       le32_to_cpu(ricb->ipv6_hash_key[i]));
	for (i = 0; i < 4; i++)
		printk(KERN_ERR PFX "ricb->ipv4_hash_key[%d] = 0x%.08x.\n", i,
		       le32_to_cpu(ricb->ipv4_hash_key[i]));
}

void ql_dump_cqicb(struct cqicb *cqicb)
{
	printk(KERN_ERR PFX "Dumping cqicb stuff...\n");

	printk(KERN_ERR PFX "cqicb->msix_vect = %d.\n", cqicb->msix_vect);
	printk(KERN_ERR PFX "cqicb->flags = %x.\n", cqicb->flags);
	printk(KERN_ERR PFX "cqicb->len = %d.\n", le16_to_cpu(cqicb->len));
	printk(KERN_ERR PFX "cqicb->addr = 0x%llx.\n",
	       (unsigned long long) le64_to_cpu(cqicb->addr));
	printk(KERN_ERR PFX "cqicb->prod_idx_addr = 0x%llx.\n",
	       (unsigned long long) le64_to_cpu(cqicb->prod_idx_addr));
	printk(KERN_ERR PFX "cqicb->pkt_delay = 0x%.04x.\n",
	       le16_to_cpu(cqicb->pkt_delay));
	printk(KERN_ERR PFX "cqicb->irq_delay = 0x%.04x.\n",
	       le16_to_cpu(cqicb->irq_delay));
	printk(KERN_ERR PFX "cqicb->lbq_addr = 0x%llx.\n",
	       (unsigned long long) le64_to_cpu(cqicb->lbq_addr));
	printk(KERN_ERR PFX "cqicb->lbq_buf_size = 0x%.04x.\n",
	       le16_to_cpu(cqicb->lbq_buf_size));
	printk(KERN_ERR PFX "cqicb->lbq_len = 0x%.04x.\n",
	       le16_to_cpu(cqicb->lbq_len));
	printk(KERN_ERR PFX "cqicb->sbq_addr = 0x%llx.\n",
	       (unsigned long long) le64_to_cpu(cqicb->sbq_addr));
	printk(KERN_ERR PFX "cqicb->sbq_buf_size = 0x%.04x.\n",
	       le16_to_cpu(cqicb->sbq_buf_size));
	printk(KERN_ERR PFX "cqicb->sbq_len = 0x%.04x.\n",
	       le16_to_cpu(cqicb->sbq_len));
}

void ql_dump_rx_ring(struct rx_ring *rx_ring)
{
	if (rx_ring == NULL)
		return;
	printk(KERN_ERR PFX
	       "===================== Dumping rx_ring %d ===============.\n",
	       rx_ring->cq_id);
	printk(KERN_ERR PFX "Dumping rx_ring %d, type = %s%s%s.\n",
	       rx_ring->cq_id, rx_ring->type == DEFAULT_Q ? "DEFAULT" : "",
	       rx_ring->type == TX_Q ? "OUTBOUND COMPLETIONS" : "",
	       rx_ring->type == RX_Q ? "INBOUND_COMPLETIONS" : "");
	printk(KERN_ERR PFX "rx_ring->cqicb = %p.\n", &rx_ring->cqicb);
	printk(KERN_ERR PFX "rx_ring->cq_base = %p.\n", rx_ring->cq_base);
	printk(KERN_ERR PFX "rx_ring->cq_base_dma = %llx.\n",
	       (unsigned long long) rx_ring->cq_base_dma);
	printk(KERN_ERR PFX "rx_ring->cq_size = %d.\n", rx_ring->cq_size);
	printk(KERN_ERR PFX "rx_ring->cq_len = %d.\n", rx_ring->cq_len);
	printk(KERN_ERR PFX
	       "rx_ring->prod_idx_sh_reg, addr = 0x%p, value = %d.\n",
	       rx_ring->prod_idx_sh_reg,
	       rx_ring->prod_idx_sh_reg
			? ql_read_sh_reg(rx_ring->prod_idx_sh_reg) : 0);
	printk(KERN_ERR PFX "rx_ring->prod_idx_sh_reg_dma = %llx.\n",
	       (unsigned long long) rx_ring->prod_idx_sh_reg_dma);
	printk(KERN_ERR PFX "rx_ring->cnsmr_idx_db_reg = %p.\n",
	       rx_ring->cnsmr_idx_db_reg);
	printk(KERN_ERR PFX "rx_ring->cnsmr_idx = %d.\n", rx_ring->cnsmr_idx);
	printk(KERN_ERR PFX "rx_ring->curr_entry = %p.\n", rx_ring->curr_entry);
	printk(KERN_ERR PFX "rx_ring->valid_db_reg = %p.\n",
	       rx_ring->valid_db_reg);

	printk(KERN_ERR PFX "rx_ring->lbq_base = %p.\n", rx_ring->lbq_base);
	printk(KERN_ERR PFX "rx_ring->lbq_base_dma = %llx.\n",
	       (unsigned long long) rx_ring->lbq_base_dma);
	printk(KERN_ERR PFX "rx_ring->lbq_base_indirect = %p.\n",
	       rx_ring->lbq_base_indirect);
	printk(KERN_ERR PFX "rx_ring->lbq_base_indirect_dma = %llx.\n",
	       (unsigned long long) rx_ring->lbq_base_indirect_dma);
	printk(KERN_ERR PFX "rx_ring->lbq = %p.\n", rx_ring->lbq);
	printk(KERN_ERR PFX "rx_ring->lbq_len = %d.\n", rx_ring->lbq_len);
	printk(KERN_ERR PFX "rx_ring->lbq_size = %d.\n", rx_ring->lbq_size);
	printk(KERN_ERR PFX "rx_ring->lbq_prod_idx_db_reg = %p.\n",
	       rx_ring->lbq_prod_idx_db_reg);
	printk(KERN_ERR PFX "rx_ring->lbq_prod_idx = %d.\n",
	       rx_ring->lbq_prod_idx);
	printk(KERN_ERR PFX "rx_ring->lbq_curr_idx = %d.\n",
	       rx_ring->lbq_curr_idx);
	printk(KERN_ERR PFX "rx_ring->lbq_clean_idx = %d.\n",
	       rx_ring->lbq_clean_idx);
	printk(KERN_ERR PFX "rx_ring->lbq_free_cnt = %d.\n",
	       rx_ring->lbq_free_cnt);
	printk(KERN_ERR PFX "rx_ring->lbq_buf_size = %d.\n",
	       rx_ring->lbq_buf_size);

	printk(KERN_ERR PFX "rx_ring->sbq_base = %p.\n", rx_ring->sbq_base);
	printk(KERN_ERR PFX "rx_ring->sbq_base_dma = %llx.\n",
	       (unsigned long long) rx_ring->sbq_base_dma);
	printk(KERN_ERR PFX "rx_ring->sbq_base_indirect = %p.\n",
	       rx_ring->sbq_base_indirect);
	printk(KERN_ERR PFX "rx_ring->sbq_base_indirect_dma = %llx.\n",
	       (unsigned long long) rx_ring->sbq_base_indirect_dma);
	printk(KERN_ERR PFX "rx_ring->sbq = %p.\n", rx_ring->sbq);
	printk(KERN_ERR PFX "rx_ring->sbq_len = %d.\n", rx_ring->sbq_len);
	printk(KERN_ERR PFX "rx_ring->sbq_size = %d.\n", rx_ring->sbq_size);
	printk(KERN_ERR PFX "rx_ring->sbq_prod_idx_db_reg addr = %p.\n",
	       rx_ring->sbq_prod_idx_db_reg);
	printk(KERN_ERR PFX "rx_ring->sbq_prod_idx = %d.\n",
	       rx_ring->sbq_prod_idx);
	printk(KERN_ERR PFX "rx_ring->sbq_curr_idx = %d.\n",
	       rx_ring->sbq_curr_idx);
	printk(KERN_ERR PFX "rx_ring->sbq_clean_idx = %d.\n",
	       rx_ring->sbq_clean_idx);
	printk(KERN_ERR PFX "rx_ring->sbq_free_cnt = %d.\n",
	       rx_ring->sbq_free_cnt);
	printk(KERN_ERR PFX "rx_ring->sbq_buf_size = %d.\n",
	       rx_ring->sbq_buf_size);
	printk(KERN_ERR PFX "rx_ring->cq_id = %d.\n", rx_ring->cq_id);
	printk(KERN_ERR PFX "rx_ring->irq = %d.\n", rx_ring->irq);
	printk(KERN_ERR PFX "rx_ring->cpu = %d.\n", rx_ring->cpu);
	printk(KERN_ERR PFX "rx_ring->qdev = %p.\n", rx_ring->qdev);
}

void ql_dump_hw_cb(struct ql_adapter *qdev, int size, u32 bit, u16 q_id)
{
	void *ptr;

	printk(KERN_ERR PFX "%s: Enter.\n", __func__);

	ptr = kmalloc(size, GFP_ATOMIC);
	if (ptr == NULL) {
		printk(KERN_ERR PFX "%s: Couldn't allocate a buffer.\n",
		       __func__);
		return;
	}

	if (ql_write_cfg(qdev, ptr, size, bit, q_id)) {
		printk(KERN_ERR "%s: Failed to upload control block!\n",
		       __func__);
		goto fail_it;
	}
	switch (bit) {
	case CFG_DRQ:
		ql_dump_wqicb((struct wqicb *)ptr);
		break;
	case CFG_DCQ:
		ql_dump_cqicb((struct cqicb *)ptr);
		break;
	case CFG_DR:
		ql_dump_ricb((struct ricb *)ptr);
		break;
	default:
		printk(KERN_ERR PFX "%s: Invalid bit value = %x.\n",
		       __func__, bit);
		break;
	}
fail_it:
	kfree(ptr);
}
#endif

#ifdef QL_OB_DUMP
void ql_dump_tx_desc(struct tx_buf_desc *tbd)
{
	printk(KERN_ERR PFX "tbd->addr  = 0x%llx\n",
	       le64_to_cpu((u64) tbd->addr));
	printk(KERN_ERR PFX "tbd->len   = %d\n",
	       le32_to_cpu(tbd->len & TX_DESC_LEN_MASK));
	printk(KERN_ERR PFX "tbd->flags = %s %s\n",
	       tbd->len & TX_DESC_C ? "C" : ".",
	       tbd->len & TX_DESC_E ? "E" : ".");
	tbd++;
	printk(KERN_ERR PFX "tbd->addr  = 0x%llx\n",
	       le64_to_cpu((u64) tbd->addr));
	printk(KERN_ERR PFX "tbd->len   = %d\n",
	       le32_to_cpu(tbd->len & TX_DESC_LEN_MASK));
	printk(KERN_ERR PFX "tbd->flags = %s %s\n",
	       tbd->len & TX_DESC_C ? "C" : ".",
	       tbd->len & TX_DESC_E ? "E" : ".");
	tbd++;
	printk(KERN_ERR PFX "tbd->addr  = 0x%llx\n",
	       le64_to_cpu((u64) tbd->addr));
	printk(KERN_ERR PFX "tbd->len   = %d\n",
	       le32_to_cpu(tbd->len & TX_DESC_LEN_MASK));
	printk(KERN_ERR PFX "tbd->flags = %s %s\n",
	       tbd->len & TX_DESC_C ? "C" : ".",
	       tbd->len & TX_DESC_E ? "E" : ".");

}

void ql_dump_ob_mac_iocb(struct ob_mac_iocb_req *ob_mac_iocb)
{
	struct ob_mac_tso_iocb_req *ob_mac_tso_iocb =
	    (struct ob_mac_tso_iocb_req *)ob_mac_iocb;
	struct tx_buf_desc *tbd;
	u16 frame_len;

	printk(KERN_ERR PFX "%s\n", __func__);
	printk(KERN_ERR PFX "opcode         = %s\n",
	       (ob_mac_iocb->opcode == OPCODE_OB_MAC_IOCB) ? "MAC" : "TSO");
	printk(KERN_ERR PFX "flags1          = %s %s %s %s %s\n",
	       ob_mac_tso_iocb->flags1 & OB_MAC_TSO_IOCB_OI ? "OI" : "",
	       ob_mac_tso_iocb->flags1 & OB_MAC_TSO_IOCB_I ? "I" : "",
	       ob_mac_tso_iocb->flags1 & OB_MAC_TSO_IOCB_D ? "D" : "",
	       ob_mac_tso_iocb->flags1 & OB_MAC_TSO_IOCB_IP4 ? "IP4" : "",
	       ob_mac_tso_iocb->flags1 & OB_MAC_TSO_IOCB_IP6 ? "IP6" : "");
	printk(KERN_ERR PFX "flags2          = %s %s %s\n",
	       ob_mac_tso_iocb->flags2 & OB_MAC_TSO_IOCB_LSO ? "LSO" : "",
	       ob_mac_tso_iocb->flags2 & OB_MAC_TSO_IOCB_UC ? "UC" : "",
	       ob_mac_tso_iocb->flags2 & OB_MAC_TSO_IOCB_TC ? "TC" : "");
	printk(KERN_ERR PFX "flags3          = %s %s %s\n",
	       ob_mac_tso_iocb->flags3 & OB_MAC_TSO_IOCB_IC ? "IC" : "",
	       ob_mac_tso_iocb->flags3 & OB_MAC_TSO_IOCB_DFP ? "DFP" : "",
	       ob_mac_tso_iocb->flags3 & OB_MAC_TSO_IOCB_V ? "V" : "");
	printk(KERN_ERR PFX "tid = %x\n", ob_mac_iocb->tid);
	printk(KERN_ERR PFX "txq_idx = %d\n", ob_mac_iocb->txq_idx);
	printk(KERN_ERR PFX "vlan_tci      = %x\n", ob_mac_tso_iocb->vlan_tci);
	if (ob_mac_iocb->opcode == OPCODE_OB_MAC_TSO_IOCB) {
		printk(KERN_ERR PFX "frame_len      = %d\n",
		       le32_to_cpu(ob_mac_tso_iocb->frame_len));
		printk(KERN_ERR PFX "mss      = %d\n",
		       le16_to_cpu(ob_mac_tso_iocb->mss));
		printk(KERN_ERR PFX "prot_hdr_len   = %d\n",
		       le16_to_cpu(ob_mac_tso_iocb->total_hdrs_len));
		printk(KERN_ERR PFX "hdr_offset     = 0x%.04x\n",
		       le16_to_cpu(ob_mac_tso_iocb->net_trans_offset));
		frame_len = le32_to_cpu(ob_mac_tso_iocb->frame_len);
	} else {
		printk(KERN_ERR PFX "frame_len      = %d\n",
		       le16_to_cpu(ob_mac_iocb->frame_len));
		frame_len = le16_to_cpu(ob_mac_iocb->frame_len);
	}
	tbd = &ob_mac_iocb->tbd[0];
	ql_dump_tx_desc(tbd);
}

void ql_dump_ob_mac_rsp(struct ob_mac_iocb_rsp *ob_mac_rsp)
{
	printk(KERN_ERR PFX "%s\n", __func__);
	printk(KERN_ERR PFX "opcode         = %d\n", ob_mac_rsp->opcode);
	printk(KERN_ERR PFX "flags          = %s %s %s %s %s %s %s\n",
	       ob_mac_rsp->flags1 & OB_MAC_IOCB_RSP_OI ? "OI" : ".",
	       ob_mac_rsp->flags1 & OB_MAC_IOCB_RSP_I ? "I" : ".",
	       ob_mac_rsp->flags1 & OB_MAC_IOCB_RSP_E ? "E" : ".",
	       ob_mac_rsp->flags1 & OB_MAC_IOCB_RSP_S ? "S" : ".",
	       ob_mac_rsp->flags1 & OB_MAC_IOCB_RSP_L ? "L" : ".",
	       ob_mac_rsp->flags1 & OB_MAC_IOCB_RSP_P ? "P" : ".",
	       ob_mac_rsp->flags2 & OB_MAC_IOCB_RSP_B ? "B" : ".");
	printk(KERN_ERR PFX "tid = %x\n", ob_mac_rsp->tid);
}
#endif

#ifdef QL_IB_DUMP
void ql_dump_ib_mac_rsp(struct ib_mac_iocb_rsp *ib_mac_rsp)
{
	printk(KERN_ERR PFX "%s\n", __func__);
	printk(KERN_ERR PFX "opcode         = 0x%x\n", ib_mac_rsp->opcode);
	printk(KERN_ERR PFX "flags1 = %s%s%s%s%s%s\n",
	       ib_mac_rsp->flags1 & IB_MAC_IOCB_RSP_OI ? "OI " : "",
	       ib_mac_rsp->flags1 & IB_MAC_IOCB_RSP_I ? "I " : "",
	       ib_mac_rsp->flags1 & IB_MAC_IOCB_RSP_TE ? "TE " : "",
	       ib_mac_rsp->flags1 & IB_MAC_IOCB_RSP_NU ? "NU " : "",
	       ib_mac_rsp->flags1 & IB_MAC_IOCB_RSP_IE ? "IE " : "",
	       ib_mac_rsp->flags1 & IB_MAC_IOCB_RSP_B ? "B " : "");

	if (ib_mac_rsp->flags1 & IB_MAC_IOCB_RSP_M_MASK)
		printk(KERN_ERR PFX "%s%s%s Multicast.\n",
		       (ib_mac_rsp->flags1 & IB_MAC_IOCB_RSP_M_MASK) ==
		       IB_MAC_IOCB_RSP_M_HASH ? "Hash" : "",
		       (ib_mac_rsp->flags1 & IB_MAC_IOCB_RSP_M_MASK) ==
		       IB_MAC_IOCB_RSP_M_REG ? "Registered" : "",
		       (ib_mac_rsp->flags1 & IB_MAC_IOCB_RSP_M_MASK) ==
		       IB_MAC_IOCB_RSP_M_PROM ? "Promiscuous" : "");

	printk(KERN_ERR PFX "flags2 = %s%s%s%s%s\n",
	       (ib_mac_rsp->flags2 & IB_MAC_IOCB_RSP_P) ? "P " : "",
	       (ib_mac_rsp->flags2 & IB_MAC_IOCB_RSP_V) ? "V " : "",
	       (ib_mac_rsp->flags2 & IB_MAC_IOCB_RSP_U) ? "U " : "",
	       (ib_mac_rsp->flags2 & IB_MAC_IOCB_RSP_T) ? "T " : "",
	       (ib_mac_rsp->flags2 & IB_MAC_IOCB_RSP_FO) ? "FO " : "");

	if (ib_mac_rsp->flags2 & IB_MAC_IOCB_RSP_ERR_MASK)
		printk(KERN_ERR PFX "%s%s%s%s%s error.\n",
		       (ib_mac_rsp->flags2 & IB_MAC_IOCB_RSP_ERR_MASK) ==
		       IB_MAC_IOCB_RSP_ERR_OVERSIZE ? "oversize" : "",
		       (ib_mac_rsp->flags2 & IB_MAC_IOCB_RSP_ERR_MASK) ==
		       IB_MAC_IOCB_RSP_ERR_UNDERSIZE ? "undersize" : "",
		       (ib_mac_rsp->flags2 & IB_MAC_IOCB_RSP_ERR_MASK) ==
		       IB_MAC_IOCB_RSP_ERR_PREAMBLE ? "preamble" : "",
		       (ib_mac_rsp->flags2 & IB_MAC_IOCB_RSP_ERR_MASK) ==
		       IB_MAC_IOCB_RSP_ERR_FRAME_LEN ? "frame length" : "",
		       (ib_mac_rsp->flags2 & IB_MAC_IOCB_RSP_ERR_MASK) ==
		       IB_MAC_IOCB_RSP_ERR_CRC ? "CRC" : "");

	printk(KERN_ERR PFX "flags3 = %s%s.\n",
	       ib_mac_rsp->flags3 & IB_MAC_IOCB_RSP_DS ? "DS " : "",
	       ib_mac_rsp->flags3 & IB_MAC_IOCB_RSP_DL ? "DL " : "");

	if (ib_mac_rsp->flags3 & IB_MAC_IOCB_RSP_RSS_MASK)
		printk(KERN_ERR PFX "RSS flags = %s%s%s%s.\n",
		       ((ib_mac_rsp->flags3 & IB_MAC_IOCB_RSP_RSS_MASK) ==
			IB_MAC_IOCB_RSP_M_IPV4) ? "IPv4 RSS" : "",
		       ((ib_mac_rsp->flags3 & IB_MAC_IOCB_RSP_RSS_MASK) ==
			IB_MAC_IOCB_RSP_M_IPV6) ? "IPv6 RSS " : "",
		       ((ib_mac_rsp->flags3 & IB_MAC_IOCB_RSP_RSS_MASK) ==
			IB_MAC_IOCB_RSP_M_TCP_V4) ? "TCP/IPv4 RSS" : "",
		       ((ib_mac_rsp->flags3 & IB_MAC_IOCB_RSP_RSS_MASK) ==
			IB_MAC_IOCB_RSP_M_TCP_V6) ? "TCP/IPv6 RSS" : "");

	printk(KERN_ERR PFX "data_len	= %d\n",
	       le32_to_cpu(ib_mac_rsp->data_len));
	printk(KERN_ERR PFX "data_addr    = 0x%llx\n",
	       (unsigned long long) le64_to_cpu(ib_mac_rsp->data_addr));
	if (ib_mac_rsp->flags3 & IB_MAC_IOCB_RSP_RSS_MASK)
		printk(KERN_ERR PFX "rss    = %x\n",
		       le32_to_cpu(ib_mac_rsp->rss));
	if (ib_mac_rsp->flags2 & IB_MAC_IOCB_RSP_V)
		printk(KERN_ERR PFX "vlan_id    = %x\n",
		       le16_to_cpu(ib_mac_rsp->vlan_id));

	printk(KERN_ERR PFX "flags4 = %s%s%s.\n",
		ib_mac_rsp->flags4 & IB_MAC_IOCB_RSP_HV ? "HV " : "",
		ib_mac_rsp->flags4 & IB_MAC_IOCB_RSP_HS ? "HS " : "",
		ib_mac_rsp->flags4 & IB_MAC_IOCB_RSP_HL ? "HL " : "");

	if (ib_mac_rsp->flags4 & IB_MAC_IOCB_RSP_HV) {
		printk(KERN_ERR PFX "hdr length	= %d.\n",
		       le32_to_cpu(ib_mac_rsp->hdr_len));
		printk(KERN_ERR PFX "hdr addr    = 0x%llx.\n",
		       (unsigned long long) le64_to_cpu(ib_mac_rsp->hdr_addr));
	}
}
#endif

#ifdef QL_ALL_DUMP
void ql_dump_all(struct ql_adapter *qdev)
{
	int i;

	QL_DUMP_REGS(qdev);
	QL_DUMP_QDEV(qdev);
	for (i = 0; i < qdev->tx_ring_count; i++) {
		QL_DUMP_TX_RING(&qdev->tx_ring[i]);
		QL_DUMP_WQICB((struct wqicb *)&qdev->tx_ring[i]);
	}
	for (i = 0; i < qdev->rx_ring_count; i++) {
		QL_DUMP_RX_RING(&qdev->rx_ring[i]);
		QL_DUMP_CQICB((struct cqicb *)&qdev->rx_ring[i]);
	}
}
#endif
