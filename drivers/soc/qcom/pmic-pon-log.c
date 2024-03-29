// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2020-2021, The Linux Foundation. All rights reserved. */
/* Copyright (c) 2022-2024, Qualcomm Innovation Center, Inc. All rights reserved. */

#include <linux/err.h>
#include <linux/ipc_logging.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/nvmem-consumer.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/string.h>

/* SDAM NVMEM register offsets: */
#define REG_SDAM_COUNT		0x45
#define REG_PUSH_PTR		0x46
#define REG_PUSH_SDAM_NUM	0x47
#define REG_FIFO_DATA_START	0x4B
#define REG_FIFO_DATA_END	0xBF

/* PMIC PON LOG binary format in the FIFO: */
struct pmic_pon_log_entry {
	u8	state;
	u8	event;
	u8	data1;
	u8	data0;
};

#define FIFO_SIZE		(REG_FIFO_DATA_END - REG_FIFO_DATA_START + 1)
#define FIFO_ENTRY_SIZE		(sizeof(struct pmic_pon_log_entry))

#define IPC_LOG_PAGES	3

struct pmic_pon_log_dev {
	struct device			*dev;
	struct pmic_pon_log_entry	*log;
	int				log_len;
	int				log_max_entries;
	void				*ipc_log;
	struct nvmem_device		**nvmem;
	int				nvmem_count;
	int				sdam_fifo_count;
};

enum pmic_pon_state {
	PMIC_PON_STATE_FAULT0		= 0x0,
	PMIC_PON_STATE_PON		= 0x1,
	PMIC_PON_STATE_POFF		= 0x2,
	PMIC_PON_STATE_ON		= 0x3,
	PMIC_PON_STATE_RESET		= 0x4,
	PMIC_PON_STATE_OFF		= 0x5,
	PMIC_PON_STATE_FAULT6		= 0x6,
	PMIC_PON_STATE_WARM_RESET	= 0x7,
};

static const char * const pmic_pon_state_label[] = {
	[PMIC_PON_STATE_FAULT0]		= "FAULT",
	[PMIC_PON_STATE_PON]		= "PON",
	[PMIC_PON_STATE_POFF]		= "POFF",
	[PMIC_PON_STATE_ON]		= "ON",
	[PMIC_PON_STATE_RESET]		= "RESET",
	[PMIC_PON_STATE_OFF]		= "OFF",
	[PMIC_PON_STATE_FAULT6]		= "FAULT",
	[PMIC_PON_STATE_WARM_RESET]	= "WARM_RESET",
};

enum pmic_pon_event {
	PMIC_PON_EVENT_PON_TRIGGER_RECEIVED	= 0x01,
	PMIC_PON_EVENT_OTP_COPY_COMPLETE	= 0x02,
	PMIC_PON_EVENT_TRIM_COMPLETE		= 0x03,
	PMIC_PON_EVENT_XVLO_CHECK_COMPLETE	= 0x04,
	PMIC_PON_EVENT_PMIC_CHECK_COMPLETE	= 0x05,
	PMIC_PON_EVENT_RESET_TRIGGER_RECEIVED	= 0x06,
	PMIC_PON_EVENT_RESET_TYPE		= 0x07,
	PMIC_PON_EVENT_WARM_RESET_COUNT		= 0x08,
	PMIC_PON_EVENT_FAULT_REASON_1_2		= 0x09,
	PMIC_PON_EVENT_FAULT_REASON_3		= 0x0A,
	PMIC_PON_EVENT_PBS_PC_DURING_FAULT	= 0x0B,
	PMIC_PON_EVENT_FUNDAMENTAL_RESET	= 0x0C,
	PMIC_PON_EVENT_PON_SEQ_START		= 0x0D,
	PMIC_PON_EVENT_PON_SUCCESS		= 0x0E,
	PMIC_PON_EVENT_WAITING_ON_PSHOLD	= 0x0F,
	PMIC_PON_EVENT_PMIC_SID1_FAULT		= 0x10,
	PMIC_PON_EVENT_PMIC_SID2_FAULT		= 0x11,
	PMIC_PON_EVENT_PMIC_SID3_FAULT		= 0x12,
	PMIC_PON_EVENT_PMIC_SID4_FAULT		= 0x13,
	PMIC_PON_EVENT_PMIC_SID5_FAULT		= 0x14,
	PMIC_PON_EVENT_PMIC_SID6_FAULT		= 0x15,
	PMIC_PON_EVENT_PMIC_SID7_FAULT		= 0x16,
	PMIC_PON_EVENT_PMIC_SID8_FAULT		= 0x17,
	PMIC_PON_EVENT_PMIC_SID9_FAULT		= 0x18,
	PMIC_PON_EVENT_PMIC_SID10_FAULT		= 0x19,
	PMIC_PON_EVENT_PMIC_SID11_FAULT		= 0x1A,
	PMIC_PON_EVENT_PMIC_SID12_FAULT		= 0x1B,
	PMIC_PON_EVENT_PMIC_SID13_FAULT		= 0x1C,
	PMIC_PON_EVENT_PMIC_VREG_READY_CHECK	= 0x20,
};

enum pmic_pon_reset_type {
	PMIC_PON_RESET_TYPE_WARM_RESET		= 0x1,
	PMIC_PON_RESET_TYPE_SHUTDOWN		= 0x4,
	PMIC_PON_RESET_TYPE_HARD_RESET		= 0x7,
};

static const char * const pmic_pon_reset_type_label[] = {
	[PMIC_PON_RESET_TYPE_WARM_RESET]	= "WARM_RESET",
	[PMIC_PON_RESET_TYPE_SHUTDOWN]		= "SHUTDOWN",
	[PMIC_PON_RESET_TYPE_HARD_RESET]	= "HARD_RESET",
};

static const char * const pmic_pon_fault_reason1[8] = {
	[0] = "GP_FAULT0",
	[1] = "GP_FAULT1",
	[2] = "GP_FAULT2",
	[3] = "GP_FAULT3",
	[4] = "MBG_FAULT",
	[5] = "OVLO",
	[6] = "UVLO",
	[7] = "AVDD_RB",
};

static const char * const pmic_pon_fault_reason2[8] = {
	[0] = "UNKNOWN(0)",
	[1] = "UNKNOWN(1)",
	[2] = "PMIC_RB",
	[3] = "FAULT_N",
	[4] = "FAULT_WATCHDOG",
	[5] = "PBS_NACK",
	[6] = "RESTART_PON",
	[7] = "OVERTEMP_STAGE3",
};

static const char * const pmic_pon_fault_reason3[8] = {
	[0] = "GP_FAULT4",
	[1] = "GP_FAULT5",
	[2] = "GP_FAULT6",
	[3] = "GP_FAULT7",
	[4] = "GP_FAULT8",
	[5] = "GP_FAULT9",
	[6] = "GP_FAULT10",
	[7] = "GP_FAULT11",
};

static const char * const pmic_pon_s3_reset_reason[8] = {
	[0] = "UNKNOWN(0)",
	[1] = "UNKNOWN(1)",
	[2] = "UNKNOWN(2)",
	[3] = "UNKNOWN(3)",
	[4] = "FAULT_N",
	[5] = "FAULT_WATCHDOG",
	[6] = "PBS_NACK",
	[7] = "KPDPWR_AND/OR_RESIN",
};

static const char * const pmic_pon_pon_pbl_status[8] = {
	[0] = "UNKNOWN(0)",
	[1] = "UNKNOWN(1)",
	[2] = "UNKNOWN(2)",
	[3] = "UNKNOWN(3)",
	[4] = "UNKNOWN(4)",
	[5] = "UNKNOWN(5)",
	[6] = "XVDD",
	[7] = "DVDD",
};

struct pmic_pon_trigger_mapping {
	u16		code;
	const char	*label;
};

static const struct pmic_pon_trigger_mapping pmic_pon_pon_trigger_map[] = {
	{0x0084, "PS_HOLD"},
	{0x0085, "HARD_RESET"},
	{0x0086, "RESIN_N"},
	{0x0087, "KPDPWR_N"},
	/* PM5100 USB PON trigger */
	{0x0202, "USB_CHARGER"},
	{0x0621, "RTC_ALARM"},
	{0x0640, "SMPL"},
	/* PMX75 USB PON trigger */
	{0x18A0, "USB_CHARGER"},
	{0x18C0, "PMIC_SID1_GPIO5"},
	/* PMI632 USB PON trigger */
	{0x2763, "USB_CHARGER"},
	/* PM8350B USB PON trigger */
	{0x31C2, "USB_CHARGER"},
	/* PM8550B USB PON trigger */
	/* PM7550BA USB PON trigger */
	{0x71C2, "USB_CHARGER"},
	/* PM7250B USB PON trigger */
	{0x8732, "USB_CHARGER"},
};

static const struct pmic_pon_trigger_mapping pmic_pon_reset_trigger_map[] = {
	{0x0080, "KPDPWR_N_S2"},
	{0x0081, "RESIN_N_S2"},
	{0x0082, "KPDPWR_N_AND_RESIN_N_S2"},
	{0x0083, "PMIC_WATCHDOG_S2"},
	{0x0084, "PS_HOLD"},
	{0x0085, "SW_RESET"},
	{0x0086, "RESIN_N_DEBOUNCE"},
	{0x0087, "KPDPWR_N_DEBOUNCE"},
	{0x21E3, "PMIC_SID2_BCL_ALARM"},
	{0x31F5, "PMIC_SID3_BCL_ALARM"},
	{0x11D0, "PMIC_SID1_OCP"},
	{0x21D0, "PMIC_SID2_OCP"},
	{0x41D0, "PMIC_SID4_OCP"},
	{0x51D0, "PMIC_SID5_OCP"},
};

static const enum pmic_pon_event pmic_pon_important_events[] = {
	PMIC_PON_EVENT_PON_TRIGGER_RECEIVED,
	PMIC_PON_EVENT_RESET_TRIGGER_RECEIVED,
	PMIC_PON_EVENT_RESET_TYPE,
	PMIC_PON_EVENT_FAULT_REASON_1_2,
	PMIC_PON_EVENT_FAULT_REASON_3,
	PMIC_PON_EVENT_FUNDAMENTAL_RESET,
	PMIC_PON_EVENT_PMIC_SID1_FAULT,
	PMIC_PON_EVENT_PMIC_SID2_FAULT,
	PMIC_PON_EVENT_PMIC_SID3_FAULT,
	PMIC_PON_EVENT_PMIC_SID4_FAULT,
	PMIC_PON_EVENT_PMIC_SID5_FAULT,
	PMIC_PON_EVENT_PMIC_SID6_FAULT,
	PMIC_PON_EVENT_PMIC_SID7_FAULT,
	PMIC_PON_EVENT_PMIC_SID8_FAULT,
	PMIC_PON_EVENT_PMIC_SID9_FAULT,
	PMIC_PON_EVENT_PMIC_SID10_FAULT,
	PMIC_PON_EVENT_PMIC_SID11_FAULT,
	PMIC_PON_EVENT_PMIC_SID12_FAULT,
	PMIC_PON_EVENT_PMIC_SID13_FAULT,
	PMIC_PON_EVENT_PMIC_VREG_READY_CHECK,
};

static bool pmic_pon_entry_is_important(const struct pmic_pon_log_entry *entry)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(pmic_pon_important_events); i++)
		if (entry->event == pmic_pon_important_events[i])
			return true;

	return false;
}

static int pmic_pon_log_read_entry(struct pmic_pon_log_dev *pon_dev,
		u32 entry_start_index, struct pmic_pon_log_entry *entry)
{
	u8 *buf = (u8 *)entry;
	int ret, len, fifo_total_size, entry_start_sdam, entry_start_addr, i;

	fifo_total_size = FIFO_SIZE * pon_dev->sdam_fifo_count;
	entry_start_index = entry_start_index % fifo_total_size;
	entry_start_sdam = entry_start_index / FIFO_SIZE;
	entry_start_addr = (entry_start_index % FIFO_SIZE)
				+ REG_FIFO_DATA_START;

	if (entry_start_addr + FIFO_ENTRY_SIZE - 1 > REG_FIFO_DATA_END) {
		/* The entry continues beyond the end of this SDAM */
		len = FIFO_SIZE - (entry_start_index % FIFO_SIZE);
		ret = nvmem_device_read(pon_dev->nvmem[entry_start_sdam],
					entry_start_addr, len, buf);
		if (ret < 0)
			return ret;
		i = (entry_start_sdam + 1) % pon_dev->sdam_fifo_count;
		ret = nvmem_device_read(pon_dev->nvmem[i], REG_FIFO_DATA_START,
					FIFO_ENTRY_SIZE - len, &buf[len]);
	} else {
		ret = nvmem_device_read(pon_dev->nvmem[entry_start_sdam],
					entry_start_addr, FIFO_ENTRY_SIZE, buf);
	}

	return ret;
}

static int pmic_pon_log_print_reason(char *buf, int buf_size, u8 data,
					const char * const *reason)
{
	int pos = 0;
	int i;
	bool first;

	if (data == 0) {
		pos += scnprintf(buf + pos, buf_size - pos, "None");
	} else {
		first = true;
		for (i = 0; i < 8; i++) {
			if (data & BIT(i)) {
				pos += scnprintf(buf + pos, buf_size - pos,
						"%s%s",
						(first ? "" : ", "), reason[i]);
				first = false;
			}
		}
	}

	return pos;
}

#define BUF_SIZE 128

static int pmic_pon_log_parse_entry(const struct pmic_pon_log_entry *entry,
		void *ipc_log)
{
	char buf[BUF_SIZE];
	const char *label = NULL;
	bool is_important;
	int pos = 0;
	int i;
	u16 data;

	data = (entry->data1 << 8) | entry->data0;
	buf[0] = '\0';
	is_important = pmic_pon_entry_is_important(entry);

	switch (entry->event) {
	case PMIC_PON_EVENT_PON_TRIGGER_RECEIVED:
		for (i = 0; i < ARRAY_SIZE(pmic_pon_pon_trigger_map); i++) {
			if (pmic_pon_pon_trigger_map[i].code == data) {
				label = pmic_pon_pon_trigger_map[i].label;
				break;
			}
		}
		pos += scnprintf(buf + pos, BUF_SIZE - pos,
				 "PON Trigger: ");
		if (label) {
			pos += scnprintf(buf + pos, BUF_SIZE - pos, "%s",
					 label);
		} else {
			pos += scnprintf(buf + pos, BUF_SIZE - pos,
					 "SID=0x%X, PID=0x%02X, IRQ=0x%X",
					 entry->data1 >> 4, (data >> 4) & 0xFF,
					 entry->data0 & 0x7);
		}
		break;
	case PMIC_PON_EVENT_OTP_COPY_COMPLETE:
		scnprintf(buf, BUF_SIZE,
			"OTP Copy Complete: last addr written=0x%04X",
			data);
		break;
	case PMIC_PON_EVENT_TRIM_COMPLETE:
		scnprintf(buf, BUF_SIZE, "Trim Complete: %u bytes written",
			data);
		break;
	case PMIC_PON_EVENT_XVLO_CHECK_COMPLETE:
		scnprintf(buf, BUF_SIZE, "XVLO Check Complete");
		break;
	case PMIC_PON_EVENT_PMIC_CHECK_COMPLETE:
		scnprintf(buf, BUF_SIZE, "PMICs Detected: SID Mask=0x%04X",
			data);
		break;
	case PMIC_PON_EVENT_RESET_TRIGGER_RECEIVED:
		for (i = 0; i < ARRAY_SIZE(pmic_pon_reset_trigger_map); i++) {
			if (pmic_pon_reset_trigger_map[i].code == data) {
				label = pmic_pon_reset_trigger_map[i].label;
				break;
			}
		}
		pos += scnprintf(buf + pos, BUF_SIZE - pos,
				 "Reset Trigger: ");
		if (label) {
			pos += scnprintf(buf + pos, BUF_SIZE - pos, "%s",
					 label);
		} else {
			pos += scnprintf(buf + pos, BUF_SIZE - pos,
					 "SID=0x%X, PID=0x%02X, IRQ=0x%X",
					 entry->data1 >> 4, (data >> 4) & 0xFF,
					 entry->data0 & 0x7);
		}
		break;
	case PMIC_PON_EVENT_RESET_TYPE:
		if (entry->data0 < ARRAY_SIZE(pmic_pon_reset_type_label) &&
		    pmic_pon_reset_type_label[entry->data0])
			scnprintf(buf, BUF_SIZE, "Reset Type: %s",
				pmic_pon_reset_type_label[entry->data0]);
		else
			scnprintf(buf, BUF_SIZE, "Reset Type: UNKNOWN (%u)",
				entry->data0);
		break;
	case PMIC_PON_EVENT_WARM_RESET_COUNT:
		scnprintf(buf, BUF_SIZE, "Warm Reset Count: %u", data);
		break;
	case PMIC_PON_EVENT_FAULT_REASON_1_2:
		if (!entry->data0 && !entry->data1)
			is_important = false;
		if (entry->data0 || !is_important) {
			pos += scnprintf(buf + pos, BUF_SIZE - pos,
					"FAULT_REASON1=");
			pos += pmic_pon_log_print_reason(buf + pos,
					BUF_SIZE - pos, entry->data0,
					pmic_pon_fault_reason1);
		}
		if (entry->data1 || !is_important) {
			pos += scnprintf(buf + pos, BUF_SIZE - pos,
					"%sFAULT_REASON2=",
					(entry->data0 || !is_important)
						? "; " : "");
			pos += pmic_pon_log_print_reason(buf + pos,
					BUF_SIZE - pos, entry->data1,
					pmic_pon_fault_reason2);
		}
		break;
	case PMIC_PON_EVENT_FAULT_REASON_3:
		if (!entry->data0)
			is_important = false;
		pos += scnprintf(buf + pos, BUF_SIZE - pos, "FAULT_REASON3=");
		pos += pmic_pon_log_print_reason(buf + pos, BUF_SIZE - pos,
					entry->data0, pmic_pon_fault_reason3);
		break;
	case PMIC_PON_EVENT_PBS_PC_DURING_FAULT:
		scnprintf(buf, BUF_SIZE, "PBS PC at Fault: 0x%04X", data);
		break;
	case PMIC_PON_EVENT_FUNDAMENTAL_RESET:
		if (!entry->data0 && !entry->data1)
			is_important = false;
		pos += scnprintf(buf + pos, BUF_SIZE - pos,
					"Fundamental Reset: ");
		if (entry->data1 || !is_important) {
			pos += scnprintf(buf + pos, BUF_SIZE - pos,
					"PON_PBL_STATUS=");
			pos += pmic_pon_log_print_reason(buf + pos,
					BUF_SIZE - pos, entry->data1,
					pmic_pon_pon_pbl_status);
		}
		if (entry->data0 || !is_important) {
			pos += scnprintf(buf + pos, BUF_SIZE - pos,
					"%sS3_RESET_REASON=",
					(entry->data1 || !is_important)
						? "; " : "");
			pos += pmic_pon_log_print_reason(buf + pos,
					BUF_SIZE - pos, entry->data0,
					pmic_pon_s3_reset_reason);
		}

		break;
	case PMIC_PON_EVENT_PON_SEQ_START:
		scnprintf(buf, BUF_SIZE, "Begin PON Sequence");
		break;
	case PMIC_PON_EVENT_PON_SUCCESS:
		scnprintf(buf, BUF_SIZE, "PON Successful");
		break;
	case PMIC_PON_EVENT_WAITING_ON_PSHOLD:
		scnprintf(buf, BUF_SIZE, "Waiting on PS_HOLD");
		break;
	case PMIC_PON_EVENT_PMIC_SID1_FAULT ... PMIC_PON_EVENT_PMIC_SID13_FAULT:
		if (!entry->data0 && !entry->data1)
			is_important = false;
		pos += scnprintf(buf + pos, BUF_SIZE - pos, "PMIC SID%u ",
			entry->event - PMIC_PON_EVENT_PMIC_SID1_FAULT + 1);
		if (entry->data0 || !is_important) {
			pos += scnprintf(buf + pos, BUF_SIZE - pos,
					"FAULT_REASON1=");
			pos += pmic_pon_log_print_reason(buf + pos,
					BUF_SIZE - pos, entry->data0,
					pmic_pon_fault_reason1);
		}
		if (entry->data1 || !is_important) {
			pos += scnprintf(buf + pos, BUF_SIZE - pos,
					"%sFAULT_REASON2=",
					(entry->data0 || !is_important)
						? "; " : "");
			pos += pmic_pon_log_print_reason(buf + pos,
					BUF_SIZE - pos, entry->data1,
					pmic_pon_fault_reason2);
		}
		break;
	case PMIC_PON_EVENT_PMIC_VREG_READY_CHECK:
		if (!data)
			is_important = false;
		scnprintf(buf, BUF_SIZE, "VREG Check: %sVREG_FAULT detected",
			data ? "" : "No ");
		break;
	default:
		scnprintf(buf, BUF_SIZE, "Unknown Event (0x%02X): data=0x%04X",
			entry->event, data);
		break;
	}

	if (is_important)
		pr_info("PMIC PON log: %s\n", buf);
	else
		pr_debug("PMIC PON log: %s\n", buf);

	if (entry->state < ARRAY_SIZE(pmic_pon_state_label))
		ipc_log_string(ipc_log, "State=%s; %s\n",
				pmic_pon_state_label[entry->state], buf);
	else
		ipc_log_string(ipc_log, "State=Unknown (0x%02X); %s\n",
				entry->state, buf);

	return 0;
}

static int pmic_pon_log_parse(struct pmic_pon_log_dev *pon_dev)
{
	int ret, i, addr_end, sdam_end, fifo_index_start, fifo_index_end, index;
	struct pmic_pon_log_entry entry;
	u8 buf;

	ret = nvmem_device_read(pon_dev->nvmem[0], REG_PUSH_PTR, 1, &buf);
	if (ret < 0)
		return ret;
	addr_end = buf;

	if (addr_end < REG_FIFO_DATA_START || addr_end > REG_FIFO_DATA_END) {
		dev_err(pon_dev->dev, "unexpected PON log end address: %02X\n",
			addr_end);
		return -EINVAL;
	}

	ret = nvmem_device_read(pon_dev->nvmem[0], REG_PUSH_SDAM_NUM, 1, &buf);
	if (ret < 0)
		return ret;
	sdam_end = buf;

	if (sdam_end >= pon_dev->sdam_fifo_count) {
		dev_err(pon_dev->dev, "unexpected PON log end SDAM index: %d\n",
			sdam_end);
		return -EINVAL;
	}

	fifo_index_end = sdam_end * FIFO_SIZE + addr_end - REG_FIFO_DATA_START;

	/*
	 * Calculate the FIFO start index from the end index assuming that the
	 * FIFO is full.
	 */
	fifo_index_start = fifo_index_end
				- pon_dev->log_max_entries * FIFO_ENTRY_SIZE;
	if (fifo_index_start < 0)
		fifo_index_start += FIFO_SIZE * pon_dev->sdam_fifo_count;

	for (i = 0; i < pon_dev->log_max_entries; i++) {
		index = fifo_index_start + i * FIFO_ENTRY_SIZE;

		ret = pmic_pon_log_read_entry(pon_dev, index, &entry);
		if (ret < 0)
			return ret;

		if (entry.state == 0 && entry.event == 0 && entry.data1 == 0 &&
		    entry.data0 == 0) {
			/*
			 * Ignore all 0 entries which correspond to unused
			 * FIFO space in the case that the FIFO has not wrapped
			 * around.
			 */
			continue;
		}

		ret = pmic_pon_log_parse_entry(&entry, pon_dev->ipc_log);
		if (ret < 0)
			return ret;

		pon_dev->log[pon_dev->log_len++] = entry;
	}

	return 0;
}

#define FAULT_REASON2_FAULT_N_MASK			BIT(3)
#define FAULT_REASON2_RESTART_PON_MASK			BIT(6)

/* Trigger a kernel panic if the last power off was caused by a PMIC fault. */
static void pmic_pon_log_fault_panic(struct pmic_pon_log_dev *pon_dev)
{
	int last_pon_success = pon_dev->log_len - 1;
	int prev_pon_success = 0;
	int warm_reset_skip_count = 0;
	bool pon_success_found = false;
	char buf[BUF_SIZE];
	u8 mask;
	int i;

	mask = (u8)~(FAULT_REASON2_RESTART_PON_MASK |
		     FAULT_REASON2_FAULT_N_MASK);
	/*
	 * Iterate over log events from newest to oldest.  Find the most recent
	 * and second most recent PON success events.  Ignore PON success events
	 * associated with a Warm Reset.
	 */
	for (i = pon_dev->log_len - 1; i >= 0; i--) {
		if (pon_dev->log[i].event == PMIC_PON_EVENT_PON_SUCCESS) {
			if (!pon_success_found) {
				last_pon_success = i;
				pon_success_found = true;
			} else if (warm_reset_skip_count > 0) {
				warm_reset_skip_count--;
			} else {
				prev_pon_success = i;
				break;
			}
		} else if (pon_dev->log[i].event ==
			   PMIC_PON_EVENT_WARM_RESET_COUNT) {
			warm_reset_skip_count = (pon_dev->log[i].data1 << 8) |
						pon_dev->log[i].data0;
		}
	}

	/*
	 * Check if a fault event occurred between the previous and last PON
	 * success events.  Trigger a kernel panic if so.
	 */
	for (i = prev_pon_success; i <= last_pon_success; i++) {
		switch (pon_dev->log[i].event) {
		case PMIC_PON_EVENT_FAULT_REASON_1_2:
			if (pon_dev->log[i].data0) {
				pmic_pon_log_print_reason(buf, BUF_SIZE,
							pon_dev->log[i].data0,
							pmic_pon_fault_reason1);
				panic("PMIC SID0 FAULT; FAULT_REASON1=%s", buf);
			} else if (pon_dev->log[i].data1 & mask) {
				pmic_pon_log_print_reason(buf, BUF_SIZE,
							pon_dev->log[i].data1,
							pmic_pon_fault_reason2);
				panic("PMIC SID0 FAULT; FAULT_REASON2=%s", buf);
			}
			break;
		case PMIC_PON_EVENT_FAULT_REASON_3:
			if (pon_dev->log[i].data0) {
				pmic_pon_log_print_reason(buf, BUF_SIZE,
							pon_dev->log[i].data0,
							pmic_pon_fault_reason3);
				panic("PMIC SID0 FAULT; FAULT_REASON3=%s", buf);
			}
			break;
		case PMIC_PON_EVENT_PMIC_SID1_FAULT ... PMIC_PON_EVENT_PMIC_SID13_FAULT:
			if (pon_dev->log[i].data0) {
				pmic_pon_log_print_reason(buf, BUF_SIZE,
							pon_dev->log[i].data0,
							pmic_pon_fault_reason1);
				panic("PMIC SID%u FAULT; FAULT_REASON1=%s",
					pon_dev->log[i].event -
					    PMIC_PON_EVENT_PMIC_SID1_FAULT + 1,
					buf);
			} else if (pon_dev->log[i].data1 & mask) {
				pmic_pon_log_print_reason(buf, BUF_SIZE,
							pon_dev->log[i].data1,
							pmic_pon_fault_reason2);
				panic("PMIC SID%u FAULT; FAULT_REASON2=%s",
					pon_dev->log[i].event -
					    PMIC_PON_EVENT_PMIC_SID1_FAULT + 1,
					buf);
			}
			break;
		default:
			break;
		}
	}
}

static int pmic_pon_log_probe(struct platform_device *pdev)
{
	struct pmic_pon_log_dev *pon_dev;
	char buf[12] = "";
	int ret, i;
	u8 reg = 0;

	pon_dev = devm_kzalloc(&pdev->dev, sizeof(*pon_dev), GFP_KERNEL);
	if (!pon_dev)
		return -ENOMEM;
	pon_dev->dev = &pdev->dev;

	ret = of_count_phandle_with_args(pdev->dev.of_node, "nvmem", NULL);
	if (ret < 0) {
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev, "failed to get nvmem count, ret=%d\n",
				ret);
		return ret;
	} else if (ret == 0) {
		dev_err(&pdev->dev, "nvmem property empty\n");
		return -EINVAL;
	}
	pon_dev->nvmem_count = ret;

	pon_dev->nvmem = devm_kcalloc(&pdev->dev, pon_dev->nvmem_count,
					sizeof(*pon_dev->nvmem), GFP_KERNEL);
	if (!pon_dev->nvmem)
		return -ENOMEM;

	for (i = 0; i < pon_dev->nvmem_count; i++) {
		scnprintf(buf, ARRAY_SIZE(buf), "pon_log%d", i);
		pon_dev->nvmem[i] = devm_nvmem_device_get(&pdev->dev, buf);
		if (IS_ERR(pon_dev->nvmem[i]) && i == 0 &&
		    PTR_ERR(pon_dev->nvmem[i]) != EPROBE_DEFER)
			pon_dev->nvmem[i] = devm_nvmem_device_get(&pdev->dev,
								  "pon_log");
		if (IS_ERR(pon_dev->nvmem[i])) {
			ret = PTR_ERR(pon_dev->nvmem[i]);
			if (ret != -EPROBE_DEFER)
				dev_err(&pdev->dev, "failed to get nvmem device %d, ret=%d\n",
					i, ret);
			return ret;
		}
	}

	/* Read how many SDAMs are used for the PON log in PMIC hardware */
	ret = nvmem_device_read(pon_dev->nvmem[0], REG_SDAM_COUNT, 1, &reg);
	if (ret < 0)
		return ret;
	pon_dev->sdam_fifo_count = reg + 1;

	if (pon_dev->sdam_fifo_count > pon_dev->nvmem_count) {
		dev_err(&pdev->dev, "Missing nvmem handles; found %d, expected %d\n",
			pon_dev->nvmem_count, pon_dev->sdam_fifo_count);
		return -ENODEV;
	}

	pon_dev->log_max_entries = FIFO_SIZE * pon_dev->sdam_fifo_count
					/ FIFO_ENTRY_SIZE;
	pon_dev->log = devm_kcalloc(&pdev->dev, pon_dev->log_max_entries,
				    sizeof(*pon_dev->log), GFP_KERNEL);
	if (!pon_dev->log)
		return -ENOMEM;

	pon_dev->ipc_log = ipc_log_context_create(IPC_LOG_PAGES, "pmic_pon", 0);
	platform_set_drvdata(pdev, pon_dev);

	ret = pmic_pon_log_parse(pon_dev);
	if (ret < 0)
		dev_err(&pdev->dev, "PMIC PON log parsing failed, ret=%d\n",
			ret);

	if (of_property_read_bool(pdev->dev.of_node, "qcom,pmic-fault-panic"))
		pmic_pon_log_fault_panic(pon_dev);

	return ret;
}

static int pmic_pon_log_remove(struct platform_device *pdev)
{
	struct pmic_pon_log_dev *pon_dev = platform_get_drvdata(pdev);

	ipc_log_context_destroy(pon_dev->ipc_log);

	return 0;
}

static const struct of_device_id pmic_pon_log_of_match[] = {
	{ .compatible = "qcom,pmic-pon-log" },
	{}
};
MODULE_DEVICE_TABLE(of, pmic_pon_log_of_match);

static struct platform_driver pmic_pon_log_driver = {
	.driver = {
		.name = "qti-pmic-pon-log",
		.of_match_table	= of_match_ptr(pmic_pon_log_of_match),
	},
	.probe = pmic_pon_log_probe,
	.remove = pmic_pon_log_remove,
};
module_platform_driver(pmic_pon_log_driver);

MODULE_DESCRIPTION("QTI PMIC PON log driver");
MODULE_LICENSE("GPL v2");
