// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2020, The Linux Foundation. All rights reserved. */

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
#define REG_PUSH_PTR		0x46
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
#define FIFO_MAX_ENTRY_COUNT	(FIFO_SIZE / FIFO_ENTRY_SIZE)

#define IPC_LOG_PAGES	3

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
	PMIC_PON_EVENT_PMIC_VREG_READY_CHECK	= 0x15,
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
	[2] = "UNKNOWN(2)",
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
	{0x0621, "RTC_ALARM"},
	{0x0640, "SMPL"},
	{0x18C0, "PMIC_SID1_GPIO5"},
	{0x31C2, "USB_CHARGER"},
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

static int pmic_pon_log_read_entry(struct nvmem_device *nvmem,
		u16 entry_start_addr, struct pmic_pon_log_entry *entry)
{
	u8 *buf = (u8 *)entry;
	int ret, len;

	if (entry_start_addr < REG_FIFO_DATA_START ||
	    entry_start_addr > REG_FIFO_DATA_END)
		return -EINVAL;

	if (entry_start_addr + FIFO_ENTRY_SIZE - 1 > REG_FIFO_DATA_END) {
		/* The entry wraps around the end of the FIFO. */
		len = REG_FIFO_DATA_END - entry_start_addr + 1;
		ret = nvmem_device_read(nvmem, entry_start_addr, len, buf);
		if (ret < 0)
			return ret;
		ret = nvmem_device_read(nvmem, REG_FIFO_DATA_START,
					FIFO_ENTRY_SIZE - len, &buf[len]);
	} else {
		ret = nvmem_device_read(nvmem, entry_start_addr,
					FIFO_ENTRY_SIZE, buf);
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
	case PMIC_PON_EVENT_PMIC_SID1_FAULT ... PMIC_PON_EVENT_PMIC_SID5_FAULT:
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

static int pmic_pon_log_parse(struct nvmem_device *nvmem, void *ipc_log)
{
	int ret, i, addr, addr_start, addr_end;
	struct pmic_pon_log_entry entry;
	u8 buf;

	ret = nvmem_device_read(nvmem, REG_PUSH_PTR, 1, &buf);
	if (ret < 0)
		return ret;
	addr_end = buf;

	/*
	 * Calculate the FIFO start address from the end address assuming that
	 * the FIFO is full.
	 */
	addr_start = addr_end - FIFO_MAX_ENTRY_COUNT * FIFO_ENTRY_SIZE;
	if (addr_start < REG_FIFO_DATA_START)
		addr_start += FIFO_SIZE;

	for (i = 0; i < FIFO_MAX_ENTRY_COUNT; i++) {
		addr = addr_start + i * FIFO_ENTRY_SIZE;
		if (addr > REG_FIFO_DATA_END)
			addr -= FIFO_SIZE;

		ret = pmic_pon_log_read_entry(nvmem, addr, &entry);
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

		ret = pmic_pon_log_parse_entry(&entry, ipc_log);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int pmic_pon_log_probe(struct platform_device *pdev)
{
	struct nvmem_device *nvmem;
	void *ipc_log;
	int ret = 0;

	nvmem = devm_nvmem_device_get(&pdev->dev, "pon_log");
	if (IS_ERR(nvmem)) {
		ret = PTR_ERR(nvmem);
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev, "failed to get nvmem device, ret=%d\n",
				ret);
		return ret;
	}

	ipc_log = ipc_log_context_create(IPC_LOG_PAGES, "pmic_pon", 0);
	platform_set_drvdata(pdev, ipc_log);

	ret = pmic_pon_log_parse(nvmem, ipc_log);
	if (ret < 0)
		dev_err(&pdev->dev, "PMIC PON log parsing failed, ret=%d\n",
			ret);

	return ret;
}

static int pmic_pon_log_remove(struct platform_device *pdev)
{
	void *ipc_log = platform_get_drvdata(pdev);

	ipc_log_context_destroy(ipc_log);

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
