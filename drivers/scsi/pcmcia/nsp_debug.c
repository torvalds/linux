/*========================================================================
    Debug routines for nsp_cs
      By: YOKOTA Hiroshi <yokota@netlab.is.tsukuba.ac.jp>

    This software may be used and distributed according to the terms of
    the GNU General Public License.
=========================================================================*/

/* $Id: nsp_debug.c,v 1.3 2003/07/26 14:21:09 elca Exp $ */

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
	int i, s;
	printk(KERN_DEBUG);
	print_opcodek(command[0]);
	/*printk(KERN_DEBUG "%s ", __FUNCTION__);*/
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

static void show_phase(Scsi_Cmnd *SCpnt)
{
	int i = SCpnt->SCp.phase;

	char *ph[] = {
		"PH_UNDETERMINED",
		"PH_ARBSTART",
		"PH_SELSTART",
		"PH_SELECTED",
		"PH_COMMAND",
		"PH_DATA",
		"PH_STATUS",
		"PH_MSG_IN",
		"PH_MSG_OUT",
		"PH_DISCONNECT",
		"PH_RESELECT"
	};

	if ( i < PH_UNDETERMINED || i > PH_RESELECT ) {
		printk(KERN_DEBUG "scsi phase: unknown(%d)\n", i);
		return;
	}

	printk(KERN_DEBUG "scsi phase: %s\n", ph[i]);

	return;
}

static void show_busphase(unsigned char stat)
{
	switch(stat) {
	case BUSPHASE_COMMAND:
		printk(KERN_DEBUG "BUSPHASE_COMMAND\n");
		break;
	case BUSPHASE_MESSAGE_IN:
		printk(KERN_DEBUG "BUSPHASE_MESSAGE_IN\n");
		break;
	case BUSPHASE_MESSAGE_OUT:
		printk(KERN_DEBUG "BUSPHASE_MESSAGE_OUT\n");
		break;
	case BUSPHASE_DATA_IN:
		printk(KERN_DEBUG "BUSPHASE_DATA_IN\n");
		break;
	case BUSPHASE_DATA_OUT:
		printk(KERN_DEBUG "BUSPHASE_DATA_OUT\n");
		break;
	case BUSPHASE_STATUS:
		printk(KERN_DEBUG "BUSPHASE_STATUS\n");
		break;
	case BUSPHASE_SELECT:
		printk(KERN_DEBUG "BUSPHASE_SELECT\n");
		break;
	default:
		printk(KERN_DEBUG "BUSPHASE_other\n");
		break;
	}
}

static void show_message(nsp_hw_data *data)
{
	int i;

	printk(KERN_DEBUG "msg:");
	for(i=0; i < data->MsgLen; i++) {
		printk(" %02x", data->MsgBuffer[i]);
	}
	printk("\n");
}

/* end */
