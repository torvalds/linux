/*
 * Workbit NinjaSCSI-32Bi/UDE PCI/CardBus SCSI Host Bus Adapter driver
 * Debug routine
 *
 * This software may be used and distributed according to the terms of
 * the GNU General Public License.
 */

/*
 * Show the command data of a command
 */
static const char unknown[] = "UNKNOWN";

static const char * group_0_commands[] = {
/* 00-03 */ "Test Unit Ready", "Rezero Unit", unknown, "Request Sense",
/* 04-07 */ "Format Unit", "Read Block Limits", unknown, "Reasssign Blocks",
/* 08-0d */ "Read (6)", unknown, "Write (6)", "Seek (6)", unknown, unknown,
/* 0e-12 */ unknown, "Read Reverse", "Write Filemarks", "Space", "Inquiry",  
/* 13-16 */ unknown, "Recover Buffered Data", "Mode Select", "Reserve",
/* 17-1b */ "Release", "Copy", "Erase", "Mode Sense", "Start/Stop Unit",
/* 1c-1d */ "Receive Diagnostic", "Send Diagnostic", 
/* 1e-1f */ "Prevent/Allow Medium Removal", unknown,
};


static const char *group_1_commands[] = {
/* 20-22 */  unknown, unknown, unknown,
/* 23-28 */ unknown, unknown, "Read Capacity", unknown, unknown, "Read (10)",
/* 29-2d */ unknown, "Write (10)", "Seek (10)", unknown, unknown,
/* 2e-31 */ "Write Verify","Verify", "Search High", "Search Equal",
/* 32-34 */ "Search Low", "Set Limits", "Prefetch or Read Position", 
/* 35-37 */ "Synchronize Cache","Lock/Unlock Cache", "Read Defect Data",
/* 38-3c */ "Medium Scan", "Compare","Copy Verify", "Write Buffer", "Read Buffer",
/* 3d-3f */ "Update Block", "Read Long",  "Write Long",
};


static const char *group_2_commands[] = {
/* 40-41 */ "Change Definition", "Write Same", 
/* 42-48 */ "Read Sub-Ch(cd)", "Read TOC", "Read Header(cd)", "Play Audio(cd)", unknown, "Play Audio MSF(cd)", "Play Audio Track/Index(cd)", 
/* 49-4f */ "Play Track Relative(10)(cd)", unknown, "Pause/Resume(cd)", "Log Select", "Log Sense", unknown, unknown,
/* 50-55 */ unknown, unknown, unknown, unknown, unknown, "Mode Select (10)",
/* 56-5b */ unknown, unknown, unknown, unknown, "Mode Sense (10)", unknown,
/* 5c-5f */ unknown, unknown, unknown,
};

#define group(opcode) (((opcode) >> 5) & 7)

#define RESERVED_GROUP  0
#define VENDOR_GROUP    1
#define NOTEXT_GROUP    2

static const char **commands[] = {
    group_0_commands, group_1_commands, group_2_commands, 
    (const char **) RESERVED_GROUP, (const char **) RESERVED_GROUP, 
    (const char **) NOTEXT_GROUP, (const char **) VENDOR_GROUP, 
    (const char **) VENDOR_GROUP
};

static const char reserved[] = "RESERVED";
static const char vendor[] = "VENDOR SPECIFIC";

static void print_opcodek(unsigned char opcode)
{
	const char **table = commands[ group(opcode) ];

	switch ((unsigned long) table) {
	case RESERVED_GROUP:
		printk("%s[%02x] ", reserved, opcode); 
		break;
	case NOTEXT_GROUP:
		printk("%s(notext)[%02x] ", unknown, opcode); 
		break;
	case VENDOR_GROUP:
		printk("%s[%02x] ", vendor, opcode); 
		break;
	default:
		if (table[opcode & 0x1f] != unknown)
			printk("%s[%02x] ", table[opcode & 0x1f], opcode);
		else
			printk("%s[%02x] ", unknown, opcode);
		break;
	}
}

static void print_commandk (unsigned char *command)
{
	int i,s;
//	printk(KERN_DEBUG);
	print_opcodek(command[0]);
	/*printk(KERN_DEBUG "%s ", __func__);*/
	if ((command[0] >> 5) == 6 ||
	    (command[0] >> 5) == 7 ) {
		s = 12; /* vender specific */
	} else {
		s = COMMAND_SIZE(command[0]);
	}

	for ( i = 1; i < s; ++i) {
		printk("%02x ", command[i]);
	}

	switch (s) {
	case 6:
		printk("LBA=%d len=%d",
		       (((unsigned int)command[1] & 0x0f) << 16) |
		       ( (unsigned int)command[2]         <<  8) |
		       ( (unsigned int)command[3]              ),
		       (unsigned int)command[4]
			);
		break;
	case 10:
		printk("LBA=%d len=%d",
		       ((unsigned int)command[2] << 24) |
		       ((unsigned int)command[3] << 16) |
		       ((unsigned int)command[4] <<  8) |
		       ((unsigned int)command[5]      ),
		       ((unsigned int)command[7] <<  8) |
		       ((unsigned int)command[8]      )
		       );
		break;
	case 12:
		printk("LBA=%d len=%d",
		       ((unsigned int)command[2] << 24) |
		       ((unsigned int)command[3] << 16) |
		       ((unsigned int)command[4] <<  8) |
		       ((unsigned int)command[5]      ),
		       ((unsigned int)command[6] << 24) |
		       ((unsigned int)command[7] << 16) |
		       ((unsigned int)command[8] <<  8) |
		       ((unsigned int)command[9]      )
		       );
		break;
	default:
		break;
	}
	printk("\n");
}

static void show_command(Scsi_Cmnd *SCpnt)
{
	print_commandk(SCpnt->cmnd);
}

static void show_busphase(unsigned char stat)
{
	switch(stat) {
	case BUSPHASE_COMMAND:
		printk( "BUSPHASE_COMMAND\n");
		break;
	case BUSPHASE_MESSAGE_IN:
		printk( "BUSPHASE_MESSAGE_IN\n");
		break;
	case BUSPHASE_MESSAGE_OUT:
		printk( "BUSPHASE_MESSAGE_OUT\n");
		break;
	case BUSPHASE_DATA_IN:
		printk( "BUSPHASE_DATA_IN\n");
		break;
	case BUSPHASE_DATA_OUT:
		printk( "BUSPHASE_DATA_OUT\n");
		break;
	case BUSPHASE_STATUS:
		printk( "BUSPHASE_STATUS\n");
		break;
	case BUSPHASE_SELECT:
		printk( "BUSPHASE_SELECT\n");
		break;
	default:
		printk( "BUSPHASE_other: 0x%x\n", stat);
		break;
	}
}

static void show_autophase(unsigned short i)
{
	printk("auto: 0x%x,", i);

	if(i & COMMAND_PHASE) {
		printk(" cmd");
	}
	if(i & DATA_IN_PHASE) {
		printk(" din");
	}
	if(i & DATA_OUT_PHASE) {
		printk(" dout");
	}
	if(i & MSGOUT_PHASE) {
		printk(" mout");
	}
	if(i & STATUS_PHASE) {
		printk(" stat");
	}
	if(i & ILLEGAL_PHASE) {
		printk(" ill");
	}
	if(i & BUS_FREE_OCCUER) {
		printk(" bfree-o");
	}
	if(i & MSG_IN_OCCUER) {
		printk(" min-o");
	}
	if(i & MSG_OUT_OCCUER) {
		printk(" mout-o");
	}
	if(i & SELECTION_TIMEOUT) {
		printk(" sel");
	}
	if(i & MSGIN_00_VALID) {
		printk(" m0");
	}
	if(i & MSGIN_02_VALID) {
		printk(" m2");
	}
	if(i & MSGIN_03_VALID) {
		printk(" m3");
	}
	if(i & MSGIN_04_VALID) {
		printk(" m4");
	}
	if(i & AUTOSCSI_BUSY) {
		printk(" busy");
	}

	printk("\n");
}

static void nsp32_print_register(int base)
{
	if (!(NSP32_DEBUG_MASK & NSP32_SPECIAL_PRINT_REGISTER))
		return;

	printk("Phase=0x%x, ", nsp32_read1(base, SCSI_BUS_MONITOR));
	printk("OldPhase=0x%x, ", nsp32_index_read1(base, OLD_SCSI_PHASE));
	printk("syncreg=0x%x, ", nsp32_read1(base, SYNC_REG));
	printk("ackwidth=0x%x, ", nsp32_read1(base, ACK_WIDTH));
	printk("sgtpaddr=0x%lx, ", nsp32_read4(base, SGT_ADR));
	printk("scsioutlatch=0x%x, ", nsp32_read1(base, SCSI_OUT_LATCH_TARGET_ID));
	printk("msgout=0x%lx, ", nsp32_read4(base, SCSI_MSG_OUT));
	printk("miscrd=0x%x, ", nsp32_index_read2(base, MISC_WR));
	printk("seltimeout=0x%x, ", nsp32_read2(base, SEL_TIME_OUT));
	printk("sreqrate=0x%x, ", nsp32_read1(base, SREQ_SMPL_RATE));
	printk("transStatus=0x%x, ", nsp32_read2(base, TRANSFER_STATUS));
	printk("reselectid=0x%x, ", nsp32_read2(base, COMMAND_CONTROL));
	printk("arbit=0x%x, ", nsp32_read1(base, ARBIT_STATUS));
	printk("BmStart=0x%lx, ", nsp32_read4(base, BM_START_ADR));
	printk("BmCount=0x%lx, ", nsp32_read4(base, BM_CNT));
	printk("SackCnt=0x%lx, ", nsp32_read4(base, SACK_CNT));
	printk("SReqCnt=0x%lx, ", nsp32_read4(base, SREQ_CNT));
	printk("SavedSackCnt=0x%lx, ", nsp32_read4(base, SAVED_SACK_CNT));
	printk("ScsiBusControl=0x%x, ", nsp32_read1(base, SCSI_BUS_CONTROL));
	printk("FifoRestCnt=0x%x, ", nsp32_read2(base, FIFO_REST_CNT));
	printk("CdbIn=0x%x, ", nsp32_read1(base, SCSI_CSB_IN));
	printk("\n");

	if (0) {
		printk("execph=0x%x, ", nsp32_read2(base, SCSI_EXECUTE_PHASE));
		printk("IrqStatus=0x%x, ", nsp32_read2(base, IRQ_STATUS));
		printk("\n");
	}
}

/* end */
