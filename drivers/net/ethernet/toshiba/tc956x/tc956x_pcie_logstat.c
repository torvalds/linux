/*
 * TC956X PCIe Logging and Statistics driver.
 *
 * tc956x_pcie_logstat.c
 *
 * Copyright (C) 2021 Toshiba Electronic Devices & Storage Corporation
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/*! History:
 *  17 Sep 2020 : Base lined
 *  VERSION	 : 00-01
 *
 *  15 Mar 2021 : Base lined
 *  VERSION     : 01-00
 */

/* ===================================
 * Include Files
 * ===================================
 */
#include "tc956x_pcie_logstat.h"
/* Ensure header file containing TC956X_PCIE_LOGSTAT is included in
 * tc956x_pcie_logstat.h
 */
#ifdef TC956X_PCIE_LOGSTAT
/* ===================================
 * Global Variables
 * ===================================
 */
/*
 * Array containing Different Available Ports.
 */
static uint8_t pcie_port[4][20] = {
	"Upstream Port",
	"Downstream Port1",
	"Downstream Port2",
	"Endpoint Port",
};

/*
 * Array containing different LTSSM states.
 */
static uint8_t ltssm_states[COUNT_LTSSM_REG_STATES][25] = {
	"Detect.Quiet",
	"Detect.Active",
	"Polling.Active",
	"Polling.Compliance",
	"Polling.Configuration",
	"Config.LinkwidthStart",
	"Config.LinkwidthAccept",
	"Config.LanenumWait",
	"Config.LanenumAccept",
	"Config.Complete",
	"Config.Idle",
	"Recovery.RcvrLock",
	"Recovery.Equalization",
	"Recovery.Speed",
	"Recovery.RcvrCfg",
	"Recovery.Idle",
	"L0",
	"L0s",
	"L1.Entry",
	"L1.Idle",
	"L2.Idle/L2.TransmitWake",
	"Reserved",
	"Disabled",
	"Loopback.Entry",
	"Loopback.Active",
	"Loopback.Exit",
	"Hot Reset",
};

/*
 * Array containing different Receive L0s sub-states.
 */
static uint8_t rx_L0s_state[4][20] = {
	"RxL0s.Inactive",
	"RxL0s.Idle",
	"RxL0s.FTS",
	"RxL0s.OutRecovery",
};

/*
 * Array containing different Transmit L0s sub-states.
 */
static uint8_t tx_L0s_state[4][15] = {
	"TxL0s.Inactive",
	"TxL0s.Idle",
	"TxL0s.FTS",
	"TxL0s.OutL0",
};

/*
 * Array containing different L1 sub-states.
 */
static uint8_t state_L1[8][20] = {
	"Inactive",
	"L1.1",
	"L1.2 entry",
	"L1.2 idle",
	"L1.2 exit",
	"L1.0",
	"Entry",
	"Exit",
};

/*
 * Array containing Data Layer Status.
 */
static uint8_t dl_state[2][20] = {
	"Non DL_Active",
	"DL_Active",
};


/* Static Variables for Analysis */
static uint8_t DlActive_Pre = LOGSTAT_DUMMY_VALUE, LinkSpeed_Pre = LOGSTAT_DUMMY_VALUE;
static uint8_t LinkWidth_Pre = LOGSTAT_DUMMY_VALUE;
static uint8_t DlActive = LOGSTAT_DUMMY_VALUE, LinkSpeed = LOGSTAT_DUMMY_VALUE, LinkWidth = LOGSTAT_DUMMY_VALUE;

/* ===================================
 * Function Definition
 * ===================================
 */

/**
 * tc956x_pcie_ioctl_state_log_summary
 *
 * \brief IOCTL Function to read and print State Log summary.
 *
 * \details This function is called whenever IOCTL TC956X_PCIE_STATE_LOG_SUMMARY
 * is invoked by user. This function prints a summary of State Log for particular
 * port after PCIE State Transition, example PCIe Lane change, speed change, etc.
 * State Log will contain following:
 * 1. LTSSM Timeout Occured(or not).
 * 2. DLL Active(or not).
 * 3. Link Speed Transition.
 * 4. Link Width Transition.
 * 4. L1 PM Substate Transition.
 * 5. TxL0s State Transition.
 * 6. RxL0s State Transition.
 * 7. Equalization Phase.
 * 8. LTSSM State Transition.
 *
 * \param[in] priv - pointer to pcie private data.
 * \param[in] data - data passed from user space.
 *
 * \return -EFAULT in case of copy failure, otherwise 0
 */
int tc956x_pcie_ioctl_state_log_summary(const struct tc956xmac_priv *priv, void __user *data)
{
	int ret = 0;
	struct tc956x_ioctl_state_log_summary ioctl_data;

	DBGPR_FUNC(priv->device, "--> %s\n", __func__);

	if ((priv == NULL) || (data == NULL))
		return -EFAULT;

	if (copy_from_user(&ioctl_data, data, sizeof(ioctl_data)))
		ret = -EFAULT;

	if (ret == 0)
		ret = tc956x_logstat_state_log_summary(priv->ioaddr, ioctl_data.port);

	return ret;
}

/**
 * tc956x_pcie_ioctl_state_log_summary
 *
 * \brief IOCTL Function to read and print State Log summary.
 *
 * \details This function performs following :
 * 1. Reads current PCIe Link Parameters
 * 2. Stop State Logging.
 * 3. Get current State Log Status.
 * 4. Set FIFO Pointer from 0 to 31 and Get Sate Log Data for each FIFO pointer.
 * 5. Analyze all 32 State Log Data.
 * 6. Print LTSSM state transition, if valid values are observed.
 *
 * NOTE: The function doesn't enable back the State Logging after Stop.
 *
 * \param[in] priv - pointer to pcie private data.
 * \param[in] data - data passed from user space.
 *
 * \return -EFAULT in case of copy failure, otherwise 0
 */
int tc956x_logstat_state_log_summary(void __iomem *pbase_addr, enum ports nport)
{
	int ret = 0;
	uint8_t state = 0, dll = 0, speed = 0, width = 0, status = 0;
	uint8_t count = 0; /* count for invalid State Log Data */
	int8_t fptr = 0; /* signed state log fifo pointer. can become negative. */
	char cur_ltssm[25];
	uint32_t val = 0, cur_state = 0;
	uint32_t fifo_array[MAX_FIFO_READ_POINTER + 1]; /* Array of State Log Read Value for each FIFO Read Ptr */

	if (pbase_addr == NULL) {
		ret = -EFAULT;
		KPRINT_INFO("%s : NULL Pointer Arguments\n", __func__);
	}

	if (ret == 0) {
		KPRINT_INFO("State Transition Log Summary : %s\n", pcie_port[nport]);
		/* Get PCIe LTSSM, DLL, Speed, Width, State Log Status & Disable Stop State Logging */
		if ((tc956x_logstat_get_pcie_cur_ltssm(pbase_addr, nport, &state) < 0)
		|| (tc956x_logstat_get_pcie_cur_dll(pbase_addr, nport, &dll) < 0)
		|| (tc956x_logstat_get_pcie_cur_speed(pbase_addr, nport, &speed) < 0)
		|| (tc956x_logstat_get_pcie_cur_width(pbase_addr, nport, &width) < 0)
		|| (tc956x_logstat_set_state_log_enable(pbase_addr, nport, STATE_LOG_DISABLE) < 0)
		|| (tc956x_logstat_get_state_log_stop_status(pbase_addr, nport, &status) < 0)) {
			ret = -1;
			goto end;
		}

		strcpy(cur_ltssm, ltssm_states[state]);
		/* State Logging Should Stop after Disabling State Log */
		/* Read State Log Data for each fifo pointer */
		for (fptr = 0; fptr <= MAX_FIFO_READ_POINTER; fptr++) {
			if ((tc956x_logstat_set_state_log_fifo_ptr(pbase_addr, nport, fptr) < 0)
			|| (tc956x_logstat_get_state_log_data(pbase_addr, nport, &val) < 0)) {
				ret = -1;
				goto end;
			}
			fifo_array[fptr] = val;
		}
		/* Reset all values */
		DlActive_Pre = LOGSTAT_DUMMY_VALUE;
		LinkSpeed_Pre = LOGSTAT_DUMMY_VALUE;
		LinkWidth_Pre = LOGSTAT_DUMMY_VALUE;
		DlActive = LOGSTAT_DUMMY_VALUE;
		LinkSpeed = LOGSTAT_DUMMY_VALUE;
		LinkWidth = LOGSTAT_DUMMY_VALUE;

		/* Analyze State Log Data using state log of each fifo pointer */
		for (fptr = MAX_FIFO_READ_POINTER; fptr >= 0; fptr--) {
			if (fifo_array[fptr] != INVALID_STATE_LOG) {
				cur_state = fifo_array[fptr];
				/* Start analyzing only after prev_state is set */
				ret = tc956x_logstat_state_log_analyze(cur_state);
				if (ret < 0)
					goto end;

				count++;
			} else {
				continue;
			}
		}

		if (count == 0) {
			KPRINT_INFO("==> LTSSM is not changed\n");
			KPRINT_INFO("Speed:Gen%d, Width:x%d, LTSSM:%s, DLL:%d\n", speed, width, cur_ltssm, dll);
			ret = 0;
		}

		if (status == STATE_LOG_STOP)
			KPRINT_INFO("STATE LOGGING is stopped\n");
	}
end:
	return ret;
}

/**
 * tc956x_logstat_get_state_log_stop_status
 *
 * \brief Function to get State Log Stop Status.
 *
 * \details This function reads the State Logging Status from register.
 * PCIe State Loggging is running if value returned by function is set,
 * otherwise stopped.
 *
 * \param[in] pbase_addr - pointer to Bar4 base address.
 * \param[in] nport - log start/stop for port passed.
 * \param[out] pstop_status - state logging status (1 : stopped, 0 : still running).
 *
 * \return -EFAULT in case of copy failure, otherwise 0
 */
int tc956x_logstat_get_state_log_stop_status(void __iomem *pbase_addr, enum ports nport, uint8_t *pstop_status)
{
	int ret = 0;
	uint32_t regval = 0;
	uint32_t port_offset; /* Port Address Register Offset */

	if ((pbase_addr == NULL) || (pstop_status == NULL)) {
		ret = -EFAULT;
		KPRINT_INFO("%s : Invalid Arguments\n", __func__);
	}

	if (ret == 0) {
		port_offset = nport * STATE_LOG_REG_OFFSET;
		/* Get State Logging Stop Status */
		regval = readl(pbase_addr + TC956X_CONF_REG_NPCIEUSPLOGST + port_offset);
		*pstop_status = (regval & STOP_STATUS_MASK) >> STOP_STATUS_SHIFT;
		/* KPRINT_INFO("RD: Addr= 0x%08X, Val= 0x%08X\n", TC956X_CONF_REG_NPCIEUSPLOGST + port_offset, regval); */
	}
	return ret;
}


/**
 * tc956x_logstat_set_state_log_fifo_ptr
 *
 * \brief Function to set State Log FIFO Read Pointer.
 *
 * \details This function write the FIFO Pointer. State Logging Data will be read as per
 * FIFO Pointer set in register.
 *
 * \param[in] pbase_addr - pointer to Bar4 base address.
 * \param[in] nport - log start/stop for port passed.
 * \param[in] fifo_pointer - fifo pointer (0 to 31) for which state log to be read.
 *
 * \return -EFAULT in case of copy failure, otherwise 0
 */
int tc956x_logstat_set_state_log_fifo_ptr(void __iomem *pbase_addr, enum ports nport, uint8_t fifo_pointer)
{
	int ret = 0;
	uint32_t regval = 0;
	uint32_t port_offset; /* Port Address Register Offset */

	if ((pbase_addr == NULL) || (fifo_pointer > MAX_FIFO_READ_POINTER)) {
		ret = -EFAULT;
		KPRINT_INFO("%s : Invalid Arguments\n", __func__);
	}

	if (ret == 0) {
		port_offset = nport * STATE_LOG_REG_OFFSET;
		/* Set FIFO Read Pointer Register */
		regval = (((uint32_t)(fifo_pointer) & FIFO_READ_POINTER_MASK) >> FIFO_READ_POINTER_SHIFT);
		writel(regval, pbase_addr + TC956X_CONF_REG_NPCIEUSPLOGRDCTRL + port_offset);
		/* KPRINT_INFO("WR: Addr= 0x%08X, Val= 0x%08X\n", TC956X_CONF_REG_NPCIEUSPLOGRDCTRL + port_offset, regval); */
	}
	return ret;
}

/**
 * tc956x_logstat_get_state_log_data
 *
 * \brief Function to read State Log Data.
 *
 * \details This function reads the State Logging Data from register.
 *
 * \param[in] pbase_addr - pointer to Bar4 base address.
 * \param[in] nport - log start/stop for port passed.
 * \param[out] pstate_log_data - pointer to state log data read.
 *
 * \return -EFAULT in case of copy failure, otherwise 0
 */
int tc956x_logstat_get_state_log_data(void __iomem *pbase_addr, enum ports nport, uint32_t *pstate_log_data)
{
	int ret = 0;
	uint32_t port_offset; /* Port Address Register Offset */

	if ((pbase_addr == NULL) || (pstate_log_data == NULL)) {
		ret = -EFAULT;
		KPRINT_INFO("%s : NULL Pointer Arguments\n", __func__);
	}

	if (ret == 0) {
		port_offset = nport * STATE_LOG_REG_OFFSET;
		/* Read LTSSM Log Data Register */
		*pstate_log_data = readl(pbase_addr + TC956X_CONF_REG_NPCIEUSPLOGD + port_offset);
		/* KPRINT_INFO("RD: Addr= 0x%08X, Val= 0x%08X\n", TC956X_CONF_REG_NPCIEUSPLOGD + port_offset, *pstate_log_data); */
	}
	return ret;
}

/**
 * tc956x_logstat_state_log_analyze
 *
 * \brief Function to analyze State Log Data.
 *
 * \details This function analyze the State Logging Data as per current value passed.
 *
 * \param[in] cur_state - FIFO register data containing current state of pcie.
 *
 * \return always 0.
 */
int tc956x_logstat_state_log_analyze(uint32_t cur_state)
{
	union tc956x_logstat_State_Log_Data curr_state_log_data;
	uint8_t timeout = 0, activelane = 0, l1_substate = 0, tx_l0s = 0, rx_l0s = 0, eqphase = 0, ltssm = 0;
	uint8_t l1ss[20], txl0s_dec[20], rxl0s_dec[20], ltssm_dec[30];
	uint8_t append_str[150];

	/* Assign Previous and Current State Log Data */
	curr_state_log_data.reg_val = cur_state;

	/* Decoding State Log */
	/* LTSSM Timeout Decoding */
	if (curr_state_log_data.bitfield.fifo_read_value8 == LTSSM_TIMEOUT_OCCURRED)
		timeout = LTSSM_TIMEOUT_OCCURRED;
	else
		timeout = LTSSM_TIMEOUT_NOT_OCCURRED;

	/* DL_Active Decoding */
	DlActive_Pre = DlActive;

	if (curr_state_log_data.bitfield.fifo_read_value7 == DL_ACTIVE)
		DlActive = DL_ACTIVE;
	else
		DlActive = DL_NOT_ACTIVE;

	/* Link Speed Decoding */
	LinkSpeed_Pre = LinkSpeed;
	LinkSpeed = curr_state_log_data.bitfield.fifo_read_value6;

	/* Link Width Decoding */
	LinkWidth_Pre = LinkWidth;
	activelane = curr_state_log_data.bitfield.fifo_read_value5;
	LinkWidth = 0;
	while (activelane) {
		LinkWidth += activelane & ACTIVE_SINGLE_LANE_MASK;
		activelane = (activelane >> ACTIVE_SINGLE_LANE_SHIFT) & ACTIVE_ALL_LANE_MASK;
	}

	/* L1 PM Substate Decoding */
	l1_substate = curr_state_log_data.bitfield.fifo_read_value4;
	strcpy(l1ss, state_L1[l1_substate]);

	/* TxL0s Decoding */
	tx_l0s = curr_state_log_data.bitfield.fifo_read_value3;
	strcpy(txl0s_dec, tx_L0s_state[tx_l0s]);

	/* RxL0s Decoding */
	rx_l0s = curr_state_log_data.bitfield.fifo_read_value2;
	strcpy(rxl0s_dec, rx_L0s_state[rx_l0s]);

	/* EQ Phase Decoding */
	eqphase = curr_state_log_data.bitfield.fifo_read_value1;

	/* LTSSM Decoding */
	ltssm = curr_state_log_data.bitfield.fifo_read_value0;
	if (ltssm <= LTSSM_MAX_VALUE)
		strcpy(ltssm_dec, ltssm_states[ltssm]);

	/* Print State Log Summary */
	if (timeout == LTSSM_TIMEOUT_OCCURRED)
		KPRINT_INFO("==> LTSSM Timeout occurred!\n");
	else {
		if ((DlActive_Pre == DL_NOT_ACTIVE) && (DlActive == DL_ACTIVE))
			KPRINT_INFO("==> Linkup!\n");
		else if ((DlActive_Pre == DL_ACTIVE) && (DlActive == DL_NOT_ACTIVE))
			KPRINT_INFO("==> Link is down!\n");

		if (LinkSpeed_Pre != LOGSTAT_DUMMY_VALUE) {
			if (LinkSpeed < LinkSpeed_Pre)
				KPRINT_INFO("==> Speed down occurred! (Gen%d --> Gen%d)\n", LinkSpeed_Pre, LinkSpeed);
			else if (LinkSpeed > LinkSpeed_Pre)
				KPRINT_INFO("==> Speed up occurred! (Gen%d --> Gen%d)\n", LinkSpeed_Pre, LinkSpeed);
		}

		if ((LinkWidth_Pre != LOGSTAT_DUMMY_VALUE) && (LinkWidth_Pre != ALL_LANES_INACTIVE)) {
			if (LinkWidth < LinkWidth_Pre) {
				if (strcmp(ltssm_dec, "Detect.Active") == 0) {
					if (LinkWidth > 1)
						KPRINT_INFO("==> Receiver Detection is occurred! (Only %d Lanes is detected)\n", LinkWidth);
					else
						KPRINT_INFO("==> Receiver Detection is occurred! (Only 1 Lane is detected)\n");
				} else
					KPRINT_INFO("==> Link Width down configure occurred! (x%d --> x%d)\n", LinkWidth_Pre, LinkWidth);
			} else if (LinkWidth > LinkWidth_Pre)
				KPRINT_INFO("==> Link Width upconfigure occurred! (x%d --> x%d)\n", LinkWidth_Pre, LinkWidth);
		}

		sprintf(append_str, "--> DL_Active:%d, Speed:Gen%d, Width:x%d, LTSSM:%s", DlActive, LinkSpeed, LinkWidth, ltssm_dec);

		if ((tx_l0s != INACTIVE_L0s) && (rx_l0s != INACTIVE_L0s))
			KPRINT_INFO("%s (%d:%s, %d:%s)\n", append_str, tx_l0s, txl0s_dec, rx_l0s, rxl0s_dec);
		else if ((tx_l0s != INACTIVE_L0s) && (rx_l0s == INACTIVE_L0s))
			KPRINT_INFO("%s (%s)\n", append_str, txl0s_dec);
		else if ((tx_l0s == INACTIVE_L0s) && (rx_l0s != INACTIVE_L0s))
			KPRINT_INFO("%s (%s)\n", append_str, rxl0s_dec);
		else if (strcmp(ltssm_dec, "Recovery.Equalization"))
			KPRINT_INFO("%s (Phase %d)\n", append_str, eqphase);
		else if (l1_substate != INACTIVE_L1)
			KPRINT_INFO("%s (%s)\n", append_str, l1ss);
	}
	return 0;
}

/**
 * tc956x_pcie_ioctl_get_pcie_link_params
 *
 * \brief IOCTL Function to read PCIe Link LTSSM, DL State, Speed and Width.
 *
 * \details This function is called whenever IOCTL TC956X_PCIE_GET_PCIE_LINK_PARAMS
 * is invoked by user. This function reads and print Current LTSSM State, DL Link State,
 * Link Speed and Link Width.
 *
 * \param[in] priv - pointer to pcie private data.
 * \param[in] data - data passed from user space.
 *
 * \return -EFAULT in case of copy failure, otherwise 0
 */
int tc956x_pcie_ioctl_get_pcie_link_params(const struct tc956xmac_priv *priv, void __user *data)
{
	int ret = 0;
	struct tc956x_ioctl_pcie_link_params ioctl_data;
	struct tc956x_pcie_link_params link_param;

	DBGPR_FUNC(priv->device, "--> %s\n", __func__);

	if ((priv == NULL) || (data == NULL))
		return -EFAULT;

	memset(&link_param, 0, sizeof(link_param));

	if (copy_from_user(&ioctl_data, data, sizeof(ioctl_data)))
		ret = -EFAULT;

	if (ret == 0) {
		if ((tc956x_logstat_get_pcie_cur_ltssm(priv->ioaddr, ioctl_data.port, &(link_param.ltssm)) < 0)
		|| (tc956x_logstat_get_pcie_cur_dll(priv->ioaddr, ioctl_data.port, &(link_param.dll)) < 0)
		|| (tc956x_logstat_get_pcie_cur_speed(priv->ioaddr, ioctl_data.port, &(link_param.speed)) < 0)
		|| (tc956x_logstat_get_pcie_cur_width(priv->ioaddr, ioctl_data.port, &(link_param.width)) < 0)) {
			ret = -EFAULT;
		}
	}

	if (ret == 0) {
		if (copy_to_user((void __user *)ioctl_data.link_param, &link_param, sizeof(link_param)))
			ret = -EFAULT;
	}

	DBGPR_FUNC(priv->device, "<-- %s\n", __func__);
	return ret;
}

/**
 * tc956x_logstat_get_pcie_cur_ltssm
 *
 * \brief Function to read current PCIe Link LTSSM State.
 *
 * \details This function reads current Link Training and Status State Machine
 * State from register.
 *
 * \param[in] pbase_addr - pointer to BAR4 base address.
 * \param[in] nport - port for which to get current ltssm value.
 * \param[out] pltssm - pointer to ltssm value from register.
 *
 * \return -EFAULT in case of bad address, otherwise 0
 */
int tc956x_logstat_get_pcie_cur_ltssm(void __iomem *pbase_addr, enum ports nport, uint8_t *pltssm)
{
	int ret = 0;
	uint32_t regval;
	uint32_t reg_offset; /* Port Address Register Offset */

	if ((pbase_addr == NULL) || (pltssm == NULL)) {
		ret = -EFAULT;
		KPRINT_INFO("%s : NULL Pointer Arguments\n", __func__);
	}

	if (ret == 0) {
		reg_offset = nport * GLUE_REG_LTSSM_OFFSET;
		/* Read Current LTSSM State */
		regval = readl(pbase_addr + TC956X_GLUE_SW_USP_TEST_OUT_127_096 + reg_offset);
		*pltssm = (regval & TC956X_GLUE_LTSSM_STATE_MASK) >> TC956X_GLUE_LTSSM_STATE_SHIFT;
		KPRINT_INFO("%s : LTSSM State %s for port %s\n", __func__, ltssm_states[(*pltssm)], pcie_port[nport]);
		if ((*pltssm) > LTSSM_MAX_VALUE)
			ret = -1;
	}
	return ret;
}

/**
 * tc956x_logstat_get_pcie_cur_dll
 *
 * \brief Function to read current PCIe Link DLL Active State.
 *
 * \details This function reads current Data Link Layer Active State from register.
 *
 * \param[in] pbase_addr - pointer to BAR4 base address.
 * \param[in] nport - port for which to get current ltssm value.
 * \param[out] pdll - pointer to dll active state value from register.
 *
 * \return -EFAULT in case of bad address, otherwise 0
 */
int tc956x_logstat_get_pcie_cur_dll(void __iomem *pbase_addr, enum ports nport, uint8_t *pdll)
{
	int ret = 0;
	uint32_t regval;
	uint32_t reg_offset; /* Port Address Register Offset */

	if ((pbase_addr == NULL) || (pdll == NULL)) {
		ret = -EFAULT;
		KPRINT_INFO("%s : NULL Pointer Arguments\n", __func__);
	}

	if (ret == 0) {
		reg_offset = nport * GLUE_REG_LTSSM_OFFSET;
		/* Read DLL State */
		regval = readl(pbase_addr + TC956X_GLUE_SW_USP_TEST_OUT_127_096 + reg_offset);
		*pdll = (regval & TC956X_GLUE_DLL_MASK) >> TC956X_GLUE_DLL_SHIFT;
		KPRINT_INFO("%s : DLL State %s for port %s\n", __func__, dl_state[(*pdll)], pcie_port[nport]);
	}
	return ret;
}

/**
 * tc956x_logstat_get_pcie_cur_speed
 *
 * \brief Function to read current PCIe Link Speed.
 *
 * \details This function reads current PCIe Link Speed from register.
 *
 * \param[in] pbase_addr - pointer to BAR4 base address.
 * \param[in] nport - port for which to get current ltssm value.
 * \param[out] pspeed_val - pointer to current link speed value from register.
 *
 * \return -EFAULT in case of bad address, otherwise 0
 */
int tc956x_logstat_get_pcie_cur_speed(void __iomem *pbase_addr, enum ports nport, uint8_t *pspeed_val)
{
	int ret = 0;
	uint32_t regval;

	if ((pbase_addr == NULL) || (pspeed_val == NULL)) {
		ret = -EFAULT;
		KPRINT_INFO("%s : NULL Pointer Arguments\n", __func__);
	}

	if (ret == 0) {
		/* Read Speed */
		regval = readl(pbase_addr + TC956X_GLUE_TL_LINK_SPEED_MON);
		*pspeed_val = (regval & TC956X_GLUE_SPEED_MASK(nport)) >> TC956X_GLUE_SPEED_SHIFT(nport);
		KPRINT_INFO("%s : Link Speed Gen%d for port %s\n", __func__, (*pspeed_val), pcie_port[nport]);
	}
	return ret;
}

/**
 * tc956x_logstat_get_pcie_cur_width
 *
 * \brief Function to read current PCIe Link Width.
 *
 * \details This function reads current PCIe Link Width from register.
 *
 * \param[in] pbase_addr - pointer to BAR4 base address.
 * \param[in] nport - port for which to get current ltssm value.
 * \param[out] plane_width_val - pointer to current lane width value from register.
 *
 * \return -EFAULT in case of bad address, otherwise 0
 */
int tc956x_logstat_get_pcie_cur_width(void __iomem *pbase_addr, enum ports nport, uint8_t *plane_width_val)
{
	int ret = 0;
	uint32_t regval;

	if ((pbase_addr == NULL) || (plane_width_val == NULL)) {
		ret = -EFAULT;
		KPRINT_INFO("%s : NULL Pointer Arguments\n", __func__);
	}

	if (ret == 0) {
		/* Read Lane Width */
		regval = readl(pbase_addr + TC956X_GLUE_TL_NUM_LANES_MON);
		*plane_width_val = (regval & TC956X_GLUE_LANE_WIDTH_MASK(nport)) >> TC956X_GLUE_LANE_WIDTH_SHIFT(nport);
		KPRINT_INFO("%s : Lane Width x%d for port %s\n", __func__, (*plane_width_val), pcie_port[nport]);
	}
	return ret;
}


/**
 * tc956x_pcie_ioctl_StateLogStop
 *
 * \brief IOCTL Function to Enable and Disable State Logging.
 *
 * \details This function is called whenever IOCTL TC956X_PCIE_STATE_LOG_ENABLE
 * is invoked by user. This function set register to enable/disable state logging.
 *
 * \param[in] priv - pointer to pcie private data.
 * \param[in] data - data passed from user space.
 *
 * \return -EFAULT in case of bad address, otherwise 0.
 */
int tc956x_pcie_ioctl_state_log_enable(const struct tc956xmac_priv *priv, void __user *data)
{
	int ret = 0;
	struct tc956x_ioctl_state_log_enable ioctl_data;

	DBGPR_FUNC(priv->device, "--> %s\n", __func__);

	if ((priv == NULL) || (data == NULL))
		return -EFAULT;

	if (copy_from_user(&ioctl_data, data, sizeof(ioctl_data)))
		ret = -EFAULT;

	if (ret == 0)
		ret = tc956x_logstat_set_state_log_enable(priv->ioaddr, ioctl_data.port, ioctl_data.enable);

	DBGPR_FUNC(priv->device, "<-- %s\n", __func__);

	return ret;
}

/**
 * tc956x_logstat_set_state_log_enable
 *
 * \brief Function to Enable and Disable State Log.
 *
 * \details This function enable or disable State Logging based on mode passed.
 *
 * \param[in] pbase_addr - pointer to Bar4 base address.
 * \param[in] nport - log start/stop for port passed.
 * \param[in] mode - start or stop state logging.
 *
 * \return -EFAULT in case of bad address, otherwise 0
 */
int tc956x_logstat_set_state_log_enable(void __iomem *pbase_addr, enum ports nport, enum state_log_enable enable)
{
	int ret = 0;
	uint32_t port_offset; /* Port Address Register Offset */

	if (pbase_addr == NULL) {
		ret = -EFAULT;
		KPRINT_INFO("%s : Invalid Arguments\n", __func__);
	}

	if (ret == 0) {
		port_offset = nport * STATE_LOG_REG_OFFSET;

		if (enable == STATE_LOG_ENABLE) {
			/* Stop State Log */
			writel(STATE_LOG_DISABLE, pbase_addr + TC956X_CONF_REG_NPCIEUSPLOGCTRL + port_offset);
			/* Start State Log */
			writel(STATE_LOG_ENABLE, pbase_addr + TC956X_CONF_REG_NPCIEUSPLOGCTRL + port_offset);
			/* Verify Sate Log Enable */
			if (readl(pbase_addr + TC956X_CONF_REG_NPCIEUSPLOGCTRL + port_offset) == STATE_LOG_ENABLE)
				KPRINT_INFO("%s : Enabling State Logging for port %s\n", __func__, pcie_port[nport]);
		} else {
			/* Stop State Log */
			writel(STATE_LOG_DISABLE, pbase_addr + TC956X_CONF_REG_NPCIEUSPLOGCTRL + port_offset);
		}
		/* KPRINT_INFO("WR: Addr= 0x%08X, Val= 0x%08X\n", TC956X_CONF_REG_NPCIEUSPLOGCTRL + port_offset, enable); */
	}

	return ret;
}

#endif /* ifdef TC956X_PCIE_LOGSTAT */
