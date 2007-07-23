#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/i2o.h>

static void i2o_report_util_cmd(u8 cmd);
static void i2o_report_exec_cmd(u8 cmd);
static void i2o_report_fail_status(u8 req_status, u32 * msg);
static void i2o_report_common_status(u8 req_status);
static void i2o_report_common_dsc(u16 detailed_status);

/*
 * Used for error reporting/debugging purposes.
 * Report Cmd name, Request status, Detailed Status.
 */
void i2o_report_status(const char *severity, const char *str,
		       struct i2o_message *m)
{
	u32 *msg = (u32 *) m;
	u8 cmd = (msg[1] >> 24) & 0xFF;
	u8 req_status = (msg[4] >> 24) & 0xFF;
	u16 detailed_status = msg[4] & 0xFFFF;

	if (cmd == I2O_CMD_UTIL_EVT_REGISTER)
		return;		// No status in this reply

	printk("%s%s: ", severity, str);

	if (cmd < 0x1F)		// Utility cmd
		i2o_report_util_cmd(cmd);

	else if (cmd >= 0xA0 && cmd <= 0xEF)	// Executive cmd
		i2o_report_exec_cmd(cmd);
	else
		printk("Cmd = %0#2x, ", cmd);	// Other cmds

	if (msg[0] & MSG_FAIL) {
		i2o_report_fail_status(req_status, msg);
		return;
	}

	i2o_report_common_status(req_status);

	if (cmd < 0x1F || (cmd >= 0xA0 && cmd <= 0xEF))
		i2o_report_common_dsc(detailed_status);
	else
		printk(" / DetailedStatus = %0#4x.\n",
		       detailed_status);
}

/* Used to dump a message to syslog during debugging */
void i2o_dump_message(struct i2o_message *m)
{
#ifdef DEBUG
	u32 *msg = (u32 *) m;
	int i;
	printk(KERN_INFO "Dumping I2O message size %d @ %p\n",
	       msg[0] >> 16 & 0xffff, msg);
	for (i = 0; i < ((msg[0] >> 16) & 0xffff); i++)
		printk(KERN_INFO "  msg[%d] = %0#10x\n", i, msg[i]);
#endif
}

/*
 * Used for error reporting/debugging purposes.
 * Following fail status are common to all classes.
 * The preserved message must be handled in the reply handler.
 */
static void i2o_report_fail_status(u8 req_status, u32 * msg)
{
	static char *FAIL_STATUS[] = {
		"0x80",		/* not used */
		"SERVICE_SUSPENDED",	/* 0x81 */
		"SERVICE_TERMINATED",	/* 0x82 */
		"CONGESTION",
		"FAILURE",
		"STATE_ERROR",
		"TIME_OUT",
		"ROUTING_FAILURE",
		"INVALID_VERSION",
		"INVALID_OFFSET",
		"INVALID_MSG_FLAGS",
		"FRAME_TOO_SMALL",
		"FRAME_TOO_LARGE",
		"INVALID_TARGET_ID",
		"INVALID_INITIATOR_ID",
		"INVALID_INITIATOR_CONTEX",	/* 0x8F */
		"UNKNOWN_FAILURE"	/* 0xFF */
	};

	if (req_status == I2O_FSC_TRANSPORT_UNKNOWN_FAILURE)
		printk("TRANSPORT_UNKNOWN_FAILURE (%0#2x).\n",
		       req_status);
	else
		printk("TRANSPORT_%s.\n",
		       FAIL_STATUS[req_status & 0x0F]);

	/* Dump some details */

	printk(KERN_ERR "  InitiatorId = %d, TargetId = %d\n",
	       (msg[1] >> 12) & 0xFFF, msg[1] & 0xFFF);
	printk(KERN_ERR "  LowestVersion = 0x%02X, HighestVersion = 0x%02X\n",
	       (msg[4] >> 8) & 0xFF, msg[4] & 0xFF);
	printk(KERN_ERR "  FailingHostUnit = 0x%04X,  FailingIOP = 0x%03X\n",
	       msg[5] >> 16, msg[5] & 0xFFF);

	printk(KERN_ERR "  Severity:  0x%02X\n", (msg[4] >> 16) & 0xFF);
	if (msg[4] & (1 << 16))
		printk(KERN_DEBUG "(FormatError), "
		       "this msg can never be delivered/processed.\n");
	if (msg[4] & (1 << 17))
		printk(KERN_DEBUG "(PathError), "
		       "this msg can no longer be delivered/processed.\n");
	if (msg[4] & (1 << 18))
		printk(KERN_DEBUG "(PathState), "
		       "the system state does not allow delivery.\n");
	if (msg[4] & (1 << 19))
		printk(KERN_DEBUG
		       "(Congestion), resources temporarily not available;"
		       "do not retry immediately.\n");
}

/*
 * Used for error reporting/debugging purposes.
 * Following reply status are common to all classes.
 */
static void i2o_report_common_status(u8 req_status)
{
	static char *REPLY_STATUS[] = {
		"SUCCESS",
		"ABORT_DIRTY",
		"ABORT_NO_DATA_TRANSFER",
		"ABORT_PARTIAL_TRANSFER",
		"ERROR_DIRTY",
		"ERROR_NO_DATA_TRANSFER",
		"ERROR_PARTIAL_TRANSFER",
		"PROCESS_ABORT_DIRTY",
		"PROCESS_ABORT_NO_DATA_TRANSFER",
		"PROCESS_ABORT_PARTIAL_TRANSFER",
		"TRANSACTION_ERROR",
		"PROGRESS_REPORT"
	};

	if (req_status >= ARRAY_SIZE(REPLY_STATUS))
		printk("RequestStatus = %0#2x", req_status);
	else
		printk("%s", REPLY_STATUS[req_status]);
}

/*
 * Used for error reporting/debugging purposes.
 * Following detailed status are valid  for executive class,
 * utility class, DDM class and for transaction error replies.
 */
static void i2o_report_common_dsc(u16 detailed_status)
{
	static char *COMMON_DSC[] = {
		"SUCCESS",
		"0x01",		// not used
		"BAD_KEY",
		"TCL_ERROR",
		"REPLY_BUFFER_FULL",
		"NO_SUCH_PAGE",
		"INSUFFICIENT_RESOURCE_SOFT",
		"INSUFFICIENT_RESOURCE_HARD",
		"0x08",		// not used
		"CHAIN_BUFFER_TOO_LARGE",
		"UNSUPPORTED_FUNCTION",
		"DEVICE_LOCKED",
		"DEVICE_RESET",
		"INAPPROPRIATE_FUNCTION",
		"INVALID_INITIATOR_ADDRESS",
		"INVALID_MESSAGE_FLAGS",
		"INVALID_OFFSET",
		"INVALID_PARAMETER",
		"INVALID_REQUEST",
		"INVALID_TARGET_ADDRESS",
		"MESSAGE_TOO_LARGE",
		"MESSAGE_TOO_SMALL",
		"MISSING_PARAMETER",
		"TIMEOUT",
		"UNKNOWN_ERROR",
		"UNKNOWN_FUNCTION",
		"UNSUPPORTED_VERSION",
		"DEVICE_BUSY",
		"DEVICE_NOT_AVAILABLE"
	};

	if (detailed_status > I2O_DSC_DEVICE_NOT_AVAILABLE)
		printk(" / DetailedStatus = %0#4x.\n",
		       detailed_status);
	else
		printk(" / %s.\n", COMMON_DSC[detailed_status]);
}

/*
 * Used for error reporting/debugging purposes
 */
static void i2o_report_util_cmd(u8 cmd)
{
	switch (cmd) {
	case I2O_CMD_UTIL_NOP:
		printk("UTIL_NOP, ");
		break;
	case I2O_CMD_UTIL_ABORT:
		printk("UTIL_ABORT, ");
		break;
	case I2O_CMD_UTIL_CLAIM:
		printk("UTIL_CLAIM, ");
		break;
	case I2O_CMD_UTIL_RELEASE:
		printk("UTIL_CLAIM_RELEASE, ");
		break;
	case I2O_CMD_UTIL_CONFIG_DIALOG:
		printk("UTIL_CONFIG_DIALOG, ");
		break;
	case I2O_CMD_UTIL_DEVICE_RESERVE:
		printk("UTIL_DEVICE_RESERVE, ");
		break;
	case I2O_CMD_UTIL_DEVICE_RELEASE:
		printk("UTIL_DEVICE_RELEASE, ");
		break;
	case I2O_CMD_UTIL_EVT_ACK:
		printk("UTIL_EVENT_ACKNOWLEDGE, ");
		break;
	case I2O_CMD_UTIL_EVT_REGISTER:
		printk("UTIL_EVENT_REGISTER, ");
		break;
	case I2O_CMD_UTIL_LOCK:
		printk("UTIL_LOCK, ");
		break;
	case I2O_CMD_UTIL_LOCK_RELEASE:
		printk("UTIL_LOCK_RELEASE, ");
		break;
	case I2O_CMD_UTIL_PARAMS_GET:
		printk("UTIL_PARAMS_GET, ");
		break;
	case I2O_CMD_UTIL_PARAMS_SET:
		printk("UTIL_PARAMS_SET, ");
		break;
	case I2O_CMD_UTIL_REPLY_FAULT_NOTIFY:
		printk("UTIL_REPLY_FAULT_NOTIFY, ");
		break;
	default:
		printk("Cmd = %0#2x, ", cmd);
	}
}

/*
 * Used for error reporting/debugging purposes
 */
static void i2o_report_exec_cmd(u8 cmd)
{
	switch (cmd) {
	case I2O_CMD_ADAPTER_ASSIGN:
		printk("EXEC_ADAPTER_ASSIGN, ");
		break;
	case I2O_CMD_ADAPTER_READ:
		printk("EXEC_ADAPTER_READ, ");
		break;
	case I2O_CMD_ADAPTER_RELEASE:
		printk("EXEC_ADAPTER_RELEASE, ");
		break;
	case I2O_CMD_BIOS_INFO_SET:
		printk("EXEC_BIOS_INFO_SET, ");
		break;
	case I2O_CMD_BOOT_DEVICE_SET:
		printk("EXEC_BOOT_DEVICE_SET, ");
		break;
	case I2O_CMD_CONFIG_VALIDATE:
		printk("EXEC_CONFIG_VALIDATE, ");
		break;
	case I2O_CMD_CONN_SETUP:
		printk("EXEC_CONN_SETUP, ");
		break;
	case I2O_CMD_DDM_DESTROY:
		printk("EXEC_DDM_DESTROY, ");
		break;
	case I2O_CMD_DDM_ENABLE:
		printk("EXEC_DDM_ENABLE, ");
		break;
	case I2O_CMD_DDM_QUIESCE:
		printk("EXEC_DDM_QUIESCE, ");
		break;
	case I2O_CMD_DDM_RESET:
		printk("EXEC_DDM_RESET, ");
		break;
	case I2O_CMD_DDM_SUSPEND:
		printk("EXEC_DDM_SUSPEND, ");
		break;
	case I2O_CMD_DEVICE_ASSIGN:
		printk("EXEC_DEVICE_ASSIGN, ");
		break;
	case I2O_CMD_DEVICE_RELEASE:
		printk("EXEC_DEVICE_RELEASE, ");
		break;
	case I2O_CMD_HRT_GET:
		printk("EXEC_HRT_GET, ");
		break;
	case I2O_CMD_ADAPTER_CLEAR:
		printk("EXEC_IOP_CLEAR, ");
		break;
	case I2O_CMD_ADAPTER_CONNECT:
		printk("EXEC_IOP_CONNECT, ");
		break;
	case I2O_CMD_ADAPTER_RESET:
		printk("EXEC_IOP_RESET, ");
		break;
	case I2O_CMD_LCT_NOTIFY:
		printk("EXEC_LCT_NOTIFY, ");
		break;
	case I2O_CMD_OUTBOUND_INIT:
		printk("EXEC_OUTBOUND_INIT, ");
		break;
	case I2O_CMD_PATH_ENABLE:
		printk("EXEC_PATH_ENABLE, ");
		break;
	case I2O_CMD_PATH_QUIESCE:
		printk("EXEC_PATH_QUIESCE, ");
		break;
	case I2O_CMD_PATH_RESET:
		printk("EXEC_PATH_RESET, ");
		break;
	case I2O_CMD_STATIC_MF_CREATE:
		printk("EXEC_STATIC_MF_CREATE, ");
		break;
	case I2O_CMD_STATIC_MF_RELEASE:
		printk("EXEC_STATIC_MF_RELEASE, ");
		break;
	case I2O_CMD_STATUS_GET:
		printk("EXEC_STATUS_GET, ");
		break;
	case I2O_CMD_SW_DOWNLOAD:
		printk("EXEC_SW_DOWNLOAD, ");
		break;
	case I2O_CMD_SW_UPLOAD:
		printk("EXEC_SW_UPLOAD, ");
		break;
	case I2O_CMD_SW_REMOVE:
		printk("EXEC_SW_REMOVE, ");
		break;
	case I2O_CMD_SYS_ENABLE:
		printk("EXEC_SYS_ENABLE, ");
		break;
	case I2O_CMD_SYS_MODIFY:
		printk("EXEC_SYS_MODIFY, ");
		break;
	case I2O_CMD_SYS_QUIESCE:
		printk("EXEC_SYS_QUIESCE, ");
		break;
	case I2O_CMD_SYS_TAB_SET:
		printk("EXEC_SYS_TAB_SET, ");
		break;
	default:
		printk("Cmd = %#02x, ", cmd);
	}
}

void i2o_debug_state(struct i2o_controller *c)
{
	printk(KERN_INFO "%s: State = ", c->name);
	switch (((i2o_status_block *) c->status_block.virt)->iop_state) {
	case 0x01:
		printk("INIT\n");
		break;
	case 0x02:
		printk("RESET\n");
		break;
	case 0x04:
		printk("HOLD\n");
		break;
	case 0x05:
		printk("READY\n");
		break;
	case 0x08:
		printk("OPERATIONAL\n");
		break;
	case 0x10:
		printk("FAILED\n");
		break;
	case 0x11:
		printk("FAULTED\n");
		break;
	default:
		printk("%x (unknown !!)\n",
		       ((i2o_status_block *) c->status_block.virt)->iop_state);
	}
};

void i2o_dump_hrt(struct i2o_controller *c)
{
	u32 *rows = (u32 *) c->hrt.virt;
	u8 *p = (u8 *) c->hrt.virt;
	u8 *d;
	int count;
	int length;
	int i;
	int state;

	if (p[3] != 0) {
		printk(KERN_ERR
		       "%s: HRT table for controller is too new a version.\n",
		       c->name);
		return;
	}

	count = p[0] | (p[1] << 8);
	length = p[2];

	printk(KERN_INFO "%s: HRT has %d entries of %d bytes each.\n",
	       c->name, count, length << 2);

	rows += 2;

	for (i = 0; i < count; i++) {
		printk(KERN_INFO "Adapter %08X: ", rows[0]);
		p = (u8 *) (rows + 1);
		d = (u8 *) (rows + 2);
		state = p[1] << 8 | p[0];

		printk("TID %04X:[", state & 0xFFF);
		state >>= 12;
		if (state & (1 << 0))
			printk("H");	/* Hidden */
		if (state & (1 << 2)) {
			printk("P");	/* Present */
			if (state & (1 << 1))
				printk("C");	/* Controlled */
		}
		if (state > 9)
			printk("*");	/* Hard */

		printk("]:");

		switch (p[3] & 0xFFFF) {
		case 0:
			/* Adapter private bus - easy */
			printk("Local bus %d: I/O at 0x%04X Mem 0x%08X", p[2],
			       d[1] << 8 | d[0], *(u32 *) (d + 4));
			break;
		case 1:
			/* ISA bus */
			printk("ISA %d: CSN %d I/O at 0x%04X Mem 0x%08X", p[2],
			       d[2], d[1] << 8 | d[0], *(u32 *) (d + 4));
			break;

		case 2:	/* EISA bus */
			printk("EISA %d: Slot %d I/O at 0x%04X Mem 0x%08X",
			       p[2], d[3], d[1] << 8 | d[0], *(u32 *) (d + 4));
			break;

		case 3:	/* MCA bus */
			printk("MCA %d: Slot %d I/O at 0x%04X Mem 0x%08X", p[2],
			       d[3], d[1] << 8 | d[0], *(u32 *) (d + 4));
			break;

		case 4:	/* PCI bus */
			printk("PCI %d: Bus %d Device %d Function %d", p[2],
			       d[2], d[1], d[0]);
			break;

		case 0x80:	/* Other */
		default:
			printk("Unsupported bus type.");
			break;
		}
		printk("\n");
		rows += length;
	}
}

EXPORT_SYMBOL(i2o_dump_message);
