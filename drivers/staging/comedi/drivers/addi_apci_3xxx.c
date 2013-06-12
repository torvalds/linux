#include <linux/pci.h>

#include "../comedidev.h"
#include "comedi_fc.h"
#include "amcc_s5933.h"

#include "addi-data/addi_common.h"

#include "addi-data/addi_eeprom.c"
#include "addi-data/hwdrv_apci3xxx.c"

#ifndef COMEDI_SUBD_TTLIO
#define COMEDI_SUBD_TTLIO   11	/* Digital Input Output But TTL */
#endif

enum apci3xxx_boardid {
	BOARD_APCI3000_16,
	BOARD_APCI3000_8,
	BOARD_APCI3000_4,
	BOARD_APCI3006_16,
	BOARD_APCI3006_8,
	BOARD_APCI3006_4,
	BOARD_APCI3010_16,
	BOARD_APCI3010_8,
	BOARD_APCI3010_4,
	BOARD_APCI3016_16,
	BOARD_APCI3016_8,
	BOARD_APCI3016_4,
	BOARD_APCI3100_16_4,
	BOARD_APCI3100_8_4,
	BOARD_APCI3106_16_4,
	BOARD_APCI3106_8_4,
	BOARD_APCI3110_16_4,
	BOARD_APCI3110_8_4,
	BOARD_APCI3116_16_4,
	BOARD_APCI3116_8_4,
	BOARD_APCI3003,
	BOARD_APCI3002_16,
	BOARD_APCI3002_8,
	BOARD_APCI3002_4,
	BOARD_APCI3500,
};

static const struct addi_board apci3xxx_boardtypes[] = {
	[BOARD_APCI3000_16] = {
		.pc_DriverName		= "apci3000-16",
		.i_IorangeBase1		= 256,
		.i_PCIEeprom		= ADDIDATA_NO_EEPROM,
		.pc_EepromChip		= ADDIDATA_9054,
		.i_NbrAiChannel		= 16,
		.i_NbrAiChannelDiff	= 8,
		.i_AiChannelList	= 16,
		.i_AiMaxdata		= 4095,
		.pr_AiRangelist		= &range_apci3XXX_ai,
		.i_NbrTTLChannel	= 24,
		.b_AvailableConvertUnit	= 6,
		.ui_MinAcquisitiontimeNs = 10000,
		.interrupt		= v_APCI3XXX_Interrupt,
		.ai_config		= i_APCI3XXX_InsnConfigAnalogInput,
		.ai_read		= i_APCI3XXX_InsnReadAnalogInput,
		.ttl_config		= i_APCI3XXX_InsnConfigInitTTLIO,
		.ttl_bits		= i_APCI3XXX_InsnBitsTTLIO,
		.ttl_read		= i_APCI3XXX_InsnReadTTLIO,
		.ttl_write		= i_APCI3XXX_InsnWriteTTLIO,
	},
	[BOARD_APCI3000_8] = {
		.pc_DriverName		= "apci3000-8",
		.i_IorangeBase1		= 256,
		.i_PCIEeprom		= ADDIDATA_NO_EEPROM,
		.pc_EepromChip		= ADDIDATA_9054,
		.i_NbrAiChannel		= 8,
		.i_NbrAiChannelDiff	= 4,
		.i_AiChannelList	= 8,
		.i_AiMaxdata		= 4095,
		.pr_AiRangelist		= &range_apci3XXX_ai,
		.i_NbrTTLChannel	= 24,
		.b_AvailableConvertUnit	= 6,
		.ui_MinAcquisitiontimeNs = 10000,
		.interrupt		= v_APCI3XXX_Interrupt,
		.ai_config		= i_APCI3XXX_InsnConfigAnalogInput,
		.ai_read		= i_APCI3XXX_InsnReadAnalogInput,
		.ttl_config		= i_APCI3XXX_InsnConfigInitTTLIO,
		.ttl_bits		= i_APCI3XXX_InsnBitsTTLIO,
		.ttl_read		= i_APCI3XXX_InsnReadTTLIO,
		.ttl_write		= i_APCI3XXX_InsnWriteTTLIO,
	},
	[BOARD_APCI3000_4] = {
		.pc_DriverName		= "apci3000-4",
		.i_IorangeBase1		= 256,
		.i_PCIEeprom		= ADDIDATA_NO_EEPROM,
		.pc_EepromChip		= ADDIDATA_9054,
		.i_NbrAiChannel		= 4,
		.i_NbrAiChannelDiff	= 2,
		.i_AiChannelList	= 4,
		.i_AiMaxdata		= 4095,
		.pr_AiRangelist		= &range_apci3XXX_ai,
		.i_NbrTTLChannel	= 24,
		.b_AvailableConvertUnit	= 6,
		.ui_MinAcquisitiontimeNs = 10000,
		.interrupt		= v_APCI3XXX_Interrupt,
		.ai_config		= i_APCI3XXX_InsnConfigAnalogInput,
		.ai_read		= i_APCI3XXX_InsnReadAnalogInput,
		.ttl_config		= i_APCI3XXX_InsnConfigInitTTLIO,
		.ttl_bits		= i_APCI3XXX_InsnBitsTTLIO,
		.ttl_read		= i_APCI3XXX_InsnReadTTLIO,
		.ttl_write		= i_APCI3XXX_InsnWriteTTLIO,
	},
	[BOARD_APCI3006_16] = {
		.pc_DriverName		= "apci3006-16",
		.i_IorangeBase1		= 256,
		.i_PCIEeprom		= ADDIDATA_NO_EEPROM,
		.pc_EepromChip		= ADDIDATA_9054,
		.i_NbrAiChannel		= 16,
		.i_NbrAiChannelDiff	= 8,
		.i_AiChannelList	= 16,
		.i_AiMaxdata		= 65535,
		.pr_AiRangelist		= &range_apci3XXX_ai,
		.i_NbrTTLChannel	= 24,
		.b_AvailableConvertUnit	= 6,
		.ui_MinAcquisitiontimeNs = 10000,
		.interrupt		= v_APCI3XXX_Interrupt,
		.ai_config		= i_APCI3XXX_InsnConfigAnalogInput,
		.ai_read		= i_APCI3XXX_InsnReadAnalogInput,
		.ttl_config		= i_APCI3XXX_InsnConfigInitTTLIO,
		.ttl_bits		= i_APCI3XXX_InsnBitsTTLIO,
		.ttl_read		= i_APCI3XXX_InsnReadTTLIO,
		.ttl_write		= i_APCI3XXX_InsnWriteTTLIO,
	},
	[BOARD_APCI3006_8] = {
		.pc_DriverName		= "apci3006-8",
		.i_IorangeBase1		= 256,
		.i_PCIEeprom		= ADDIDATA_NO_EEPROM,
		.pc_EepromChip		= ADDIDATA_9054,
		.i_NbrAiChannel		= 8,
		.i_NbrAiChannelDiff	= 4,
		.i_AiChannelList	= 8,
		.i_AiMaxdata		= 65535,
		.pr_AiRangelist		= &range_apci3XXX_ai,
		.i_NbrTTLChannel	= 24,
		.b_AvailableConvertUnit	= 6,
		.ui_MinAcquisitiontimeNs = 10000,
		.interrupt		= v_APCI3XXX_Interrupt,
		.ai_config		= i_APCI3XXX_InsnConfigAnalogInput,
		.ai_read		= i_APCI3XXX_InsnReadAnalogInput,
		.ttl_config		= i_APCI3XXX_InsnConfigInitTTLIO,
		.ttl_bits		= i_APCI3XXX_InsnBitsTTLIO,
		.ttl_read		= i_APCI3XXX_InsnReadTTLIO,
		.ttl_write		= i_APCI3XXX_InsnWriteTTLIO,
	},
	[BOARD_APCI3006_4] = {
		.pc_DriverName		= "apci3006-4",
		.i_IorangeBase1		= 256,
		.i_PCIEeprom		= ADDIDATA_NO_EEPROM,
		.pc_EepromChip		= ADDIDATA_9054,
		.i_NbrAiChannel		= 4,
		.i_NbrAiChannelDiff	= 2,
		.i_AiChannelList	= 4,
		.i_AiMaxdata		= 65535,
		.pr_AiRangelist		= &range_apci3XXX_ai,
		.i_NbrTTLChannel	= 24,
		.b_AvailableConvertUnit	= 6,
		.ui_MinAcquisitiontimeNs = 10000,
		.interrupt		= v_APCI3XXX_Interrupt,
		.ai_config		= i_APCI3XXX_InsnConfigAnalogInput,
		.ai_read		= i_APCI3XXX_InsnReadAnalogInput,
		.ttl_config		= i_APCI3XXX_InsnConfigInitTTLIO,
		.ttl_bits		= i_APCI3XXX_InsnBitsTTLIO,
		.ttl_read		= i_APCI3XXX_InsnReadTTLIO,
		.ttl_write		= i_APCI3XXX_InsnWriteTTLIO,
	},
	[BOARD_APCI3010_16] = {
		.pc_DriverName		= "apci3010-16",
		.i_IorangeBase1		= 256,
		.i_PCIEeprom		= ADDIDATA_NO_EEPROM,
		.pc_EepromChip		= ADDIDATA_9054,
		.i_NbrAiChannel		= 16,
		.i_NbrAiChannelDiff	= 8,
		.i_AiChannelList	= 16,
		.i_AiMaxdata		= 4095,
		.pr_AiRangelist		= &range_apci3XXX_ai,
		.i_NbrDiChannel		= 4,
		.i_NbrDoChannel		= 4,
		.i_DoMaxdata		= 1,
		.i_NbrTTLChannel	= 24,
		.b_AvailableConvertUnit	= 6,
		.ui_MinAcquisitiontimeNs = 5000,
		.interrupt		= v_APCI3XXX_Interrupt,
		.ai_config		= i_APCI3XXX_InsnConfigAnalogInput,
		.ai_read		= i_APCI3XXX_InsnReadAnalogInput,
		.di_bits		= apci3xxx_di_insn_bits,
		.ttl_config		= i_APCI3XXX_InsnConfigInitTTLIO,
		.ttl_bits		= i_APCI3XXX_InsnBitsTTLIO,
		.ttl_read		= i_APCI3XXX_InsnReadTTLIO,
		.ttl_write		= i_APCI3XXX_InsnWriteTTLIO,
	},
	[BOARD_APCI3010_8] = {
		.pc_DriverName		= "apci3010-8",
		.i_IorangeBase1		= 256,
		.i_PCIEeprom		= ADDIDATA_NO_EEPROM,
		.pc_EepromChip		= ADDIDATA_9054,
		.i_NbrAiChannel		= 8,
		.i_NbrAiChannelDiff	= 4,
		.i_AiChannelList	= 8,
		.i_AiMaxdata		= 4095,
		.pr_AiRangelist		= &range_apci3XXX_ai,
		.i_NbrDiChannel		= 4,
		.i_NbrDoChannel		= 4,
		.i_DoMaxdata		= 1,
		.i_NbrTTLChannel	= 24,
		.b_AvailableConvertUnit	= 6,
		.ui_MinAcquisitiontimeNs = 5000,
		.interrupt		= v_APCI3XXX_Interrupt,
		.ai_config		= i_APCI3XXX_InsnConfigAnalogInput,
		.ai_read		= i_APCI3XXX_InsnReadAnalogInput,
		.di_bits		= apci3xxx_di_insn_bits,
		.ttl_config		= i_APCI3XXX_InsnConfigInitTTLIO,
		.ttl_bits		= i_APCI3XXX_InsnBitsTTLIO,
		.ttl_read		= i_APCI3XXX_InsnReadTTLIO,
		.ttl_write		= i_APCI3XXX_InsnWriteTTLIO,
	},
	[BOARD_APCI3010_4] = {
		.pc_DriverName		= "apci3010-4",
		.i_IorangeBase1		= 256,
		.i_PCIEeprom		= ADDIDATA_NO_EEPROM,
		.pc_EepromChip		= ADDIDATA_9054,
		.i_NbrAiChannel		= 4,
		.i_NbrAiChannelDiff	= 2,
		.i_AiChannelList	= 4,
		.i_AiMaxdata		= 4095,
		.pr_AiRangelist		= &range_apci3XXX_ai,
		.i_NbrDiChannel		= 4,
		.i_NbrDoChannel		= 4,
		.i_DoMaxdata		= 1,
		.i_NbrTTLChannel	= 24,
		.b_AvailableConvertUnit	= 6,
		.ui_MinAcquisitiontimeNs = 5000,
		.interrupt		= v_APCI3XXX_Interrupt,
		.ai_config		= i_APCI3XXX_InsnConfigAnalogInput,
		.ai_read		= i_APCI3XXX_InsnReadAnalogInput,
		.di_bits		= apci3xxx_di_insn_bits,
		.ttl_config		= i_APCI3XXX_InsnConfigInitTTLIO,
		.ttl_bits		= i_APCI3XXX_InsnBitsTTLIO,
		.ttl_read		= i_APCI3XXX_InsnReadTTLIO,
		.ttl_write		= i_APCI3XXX_InsnWriteTTLIO,
	},
	[BOARD_APCI3016_16] = {
		.pc_DriverName		= "apci3016-16",
		.i_IorangeBase1		= 256,
		.i_PCIEeprom		= ADDIDATA_NO_EEPROM,
		.pc_EepromChip		= ADDIDATA_9054,
		.i_NbrAiChannel		= 16,
		.i_NbrAiChannelDiff	= 8,
		.i_AiChannelList	= 16,
		.i_AiMaxdata		= 65535,
		.pr_AiRangelist		= &range_apci3XXX_ai,
		.i_NbrDiChannel		= 4,
		.i_NbrDoChannel		= 4,
		.i_DoMaxdata		= 1,
		.i_NbrTTLChannel	= 24,
		.b_AvailableConvertUnit	= 6,
		.ui_MinAcquisitiontimeNs = 5000,
		.interrupt		= v_APCI3XXX_Interrupt,
		.ai_config		= i_APCI3XXX_InsnConfigAnalogInput,
		.ai_read		= i_APCI3XXX_InsnReadAnalogInput,
		.di_bits		= apci3xxx_di_insn_bits,
		.ttl_config		= i_APCI3XXX_InsnConfigInitTTLIO,
		.ttl_bits		= i_APCI3XXX_InsnBitsTTLIO,
		.ttl_read		= i_APCI3XXX_InsnReadTTLIO,
		.ttl_write		= i_APCI3XXX_InsnWriteTTLIO,
	},
	[BOARD_APCI3016_8] = {
		.pc_DriverName		= "apci3016-8",
		.i_IorangeBase1		= 256,
		.i_PCIEeprom		= ADDIDATA_NO_EEPROM,
		.pc_EepromChip		= ADDIDATA_9054,
		.i_NbrAiChannel		= 8,
		.i_NbrAiChannelDiff	= 4,
		.i_AiChannelList	= 8,
		.i_AiMaxdata		= 65535,
		.pr_AiRangelist		= &range_apci3XXX_ai,
		.i_NbrDiChannel		= 4,
		.i_NbrDoChannel		= 4,
		.i_DoMaxdata		= 1,
		.i_NbrTTLChannel	= 24,
		.b_AvailableConvertUnit	= 6,
		.ui_MinAcquisitiontimeNs = 5000,
		.interrupt		= v_APCI3XXX_Interrupt,
		.ai_config		= i_APCI3XXX_InsnConfigAnalogInput,
		.ai_read		= i_APCI3XXX_InsnReadAnalogInput,
		.di_bits		= apci3xxx_di_insn_bits,
		.ttl_config		= i_APCI3XXX_InsnConfigInitTTLIO,
		.ttl_bits		= i_APCI3XXX_InsnBitsTTLIO,
		.ttl_read		= i_APCI3XXX_InsnReadTTLIO,
		.ttl_write		= i_APCI3XXX_InsnWriteTTLIO,
	},
	[BOARD_APCI3016_4] = {
		.pc_DriverName		= "apci3016-4",
		.i_IorangeBase1		= 256,
		.i_PCIEeprom		= ADDIDATA_NO_EEPROM,
		.pc_EepromChip		= ADDIDATA_9054,
		.i_NbrAiChannel		= 4,
		.i_NbrAiChannelDiff	= 2,
		.i_AiChannelList	= 4,
		.i_AiMaxdata		= 65535,
		.pr_AiRangelist		= &range_apci3XXX_ai,
		.i_NbrDiChannel		= 4,
		.i_NbrDoChannel		= 4,
		.i_DoMaxdata		= 1,
		.i_NbrTTLChannel	= 24,
		.b_AvailableConvertUnit	= 6,
		.ui_MinAcquisitiontimeNs = 5000,
		.interrupt		= v_APCI3XXX_Interrupt,
		.ai_config		= i_APCI3XXX_InsnConfigAnalogInput,
		.ai_read		= i_APCI3XXX_InsnReadAnalogInput,
		.di_bits		= apci3xxx_di_insn_bits,
		.ttl_config		= i_APCI3XXX_InsnConfigInitTTLIO,
		.ttl_bits		= i_APCI3XXX_InsnBitsTTLIO,
		.ttl_read		= i_APCI3XXX_InsnReadTTLIO,
		.ttl_write		= i_APCI3XXX_InsnWriteTTLIO,
	},
	[BOARD_APCI3100_16_4] = {
		.pc_DriverName		= "apci3100-16-4",
		.i_IorangeBase1		= 256,
		.i_PCIEeprom		= ADDIDATA_NO_EEPROM,
		.pc_EepromChip		= ADDIDATA_9054,
		.i_NbrAiChannel		= 16,
		.i_NbrAiChannelDiff	= 8,
		.i_AiChannelList	= 16,
		.i_NbrAoChannel		= 4,
		.i_AiMaxdata		= 4095,
		.i_AoMaxdata		= 4095,
		.pr_AiRangelist		= &range_apci3XXX_ai,
		.pr_AoRangelist		= &range_apci3XXX_ao,
		.i_NbrTTLChannel	= 24,
		.b_AvailableConvertUnit	= 6,
		.ui_MinAcquisitiontimeNs = 10000,
		.interrupt		= v_APCI3XXX_Interrupt,
		.ai_config		= i_APCI3XXX_InsnConfigAnalogInput,
		.ai_read		= i_APCI3XXX_InsnReadAnalogInput,
		.ao_write		= i_APCI3XXX_InsnWriteAnalogOutput,
		.ttl_config		= i_APCI3XXX_InsnConfigInitTTLIO,
		.ttl_bits		= i_APCI3XXX_InsnBitsTTLIO,
		.ttl_read		= i_APCI3XXX_InsnReadTTLIO,
		.ttl_write		= i_APCI3XXX_InsnWriteTTLIO,
	},
	[BOARD_APCI3100_8_4] = {
		.pc_DriverName		= "apci3100-8-4",
		.i_IorangeBase1		= 256,
		.i_PCIEeprom		= ADDIDATA_NO_EEPROM,
		.pc_EepromChip		= ADDIDATA_9054,
		.i_NbrAiChannel		= 8,
		.i_NbrAiChannelDiff	= 4,
		.i_AiChannelList	= 8,
		.i_NbrAoChannel		= 4,
		.i_AiMaxdata		= 4095,
		.i_AoMaxdata		= 4095,
		.pr_AiRangelist		= &range_apci3XXX_ai,
		.pr_AoRangelist		= &range_apci3XXX_ao,
		.i_NbrTTLChannel	= 24,
		.b_AvailableConvertUnit	= 6,
		.ui_MinAcquisitiontimeNs = 10000,
		.interrupt		= v_APCI3XXX_Interrupt,
		.ai_config		= i_APCI3XXX_InsnConfigAnalogInput,
		.ai_read		= i_APCI3XXX_InsnReadAnalogInput,
		.ao_write		= i_APCI3XXX_InsnWriteAnalogOutput,
		.ttl_config		= i_APCI3XXX_InsnConfigInitTTLIO,
		.ttl_bits		= i_APCI3XXX_InsnBitsTTLIO,
		.ttl_read		= i_APCI3XXX_InsnReadTTLIO,
		.ttl_write		= i_APCI3XXX_InsnWriteTTLIO,
	},
	[BOARD_APCI3106_16_4] = {
		.pc_DriverName		= "apci3106-16-4",
		.i_IorangeBase1		= 256,
		.i_PCIEeprom		= ADDIDATA_NO_EEPROM,
		.pc_EepromChip		= ADDIDATA_9054,
		.i_NbrAiChannel		= 16,
		.i_NbrAiChannelDiff	= 8,
		.i_AiChannelList	= 16,
		.i_NbrAoChannel		= 4,
		.i_AiMaxdata		= 65535,
		.i_AoMaxdata		= 4095,
		.pr_AiRangelist		= &range_apci3XXX_ai,
		.pr_AoRangelist		= &range_apci3XXX_ao,
		.i_NbrTTLChannel	= 24,
		.b_AvailableConvertUnit	= 6,
		.ui_MinAcquisitiontimeNs = 10000,
		.interrupt		= v_APCI3XXX_Interrupt,
		.ai_config		= i_APCI3XXX_InsnConfigAnalogInput,
		.ai_read		= i_APCI3XXX_InsnReadAnalogInput,
		.ao_write		= i_APCI3XXX_InsnWriteAnalogOutput,
		.ttl_config		= i_APCI3XXX_InsnConfigInitTTLIO,
		.ttl_bits		= i_APCI3XXX_InsnBitsTTLIO,
		.ttl_read		= i_APCI3XXX_InsnReadTTLIO,
		.ttl_write		= i_APCI3XXX_InsnWriteTTLIO,
	},
	[BOARD_APCI3106_8_4] = {
		.pc_DriverName		= "apci3106-8-4",
		.i_IorangeBase1		= 256,
		.i_PCIEeprom		= ADDIDATA_NO_EEPROM,
		.pc_EepromChip		= ADDIDATA_9054,
		.i_NbrAiChannel		= 8,
		.i_NbrAiChannelDiff	= 4,
		.i_AiChannelList	= 8,
		.i_NbrAoChannel		= 4,
		.i_AiMaxdata		= 65535,
		.i_AoMaxdata		= 4095,
		.pr_AiRangelist		= &range_apci3XXX_ai,
		.pr_AoRangelist		= &range_apci3XXX_ao,
		.i_NbrTTLChannel	= 24,
		.b_AvailableConvertUnit	= 6,
		.ui_MinAcquisitiontimeNs = 10000,
		.interrupt		= v_APCI3XXX_Interrupt,
		.ai_config		= i_APCI3XXX_InsnConfigAnalogInput,
		.ai_read		= i_APCI3XXX_InsnReadAnalogInput,
		.ao_write		= i_APCI3XXX_InsnWriteAnalogOutput,
		.ttl_config		= i_APCI3XXX_InsnConfigInitTTLIO,
		.ttl_bits		= i_APCI3XXX_InsnBitsTTLIO,
		.ttl_read		= i_APCI3XXX_InsnReadTTLIO,
		.ttl_write		= i_APCI3XXX_InsnWriteTTLIO,
	},
	[BOARD_APCI3110_16_4] = {
		.pc_DriverName		= "apci3110-16-4",
		.i_IorangeBase1		= 256,
		.i_PCIEeprom		= ADDIDATA_NO_EEPROM,
		.pc_EepromChip		= ADDIDATA_9054,
		.i_NbrAiChannel		= 16,
		.i_NbrAiChannelDiff	= 8,
		.i_AiChannelList	= 16,
		.i_NbrAoChannel		= 4,
		.i_AiMaxdata		= 4095,
		.i_AoMaxdata		= 4095,
		.pr_AiRangelist		= &range_apci3XXX_ai,
		.pr_AoRangelist		= &range_apci3XXX_ao,
		.i_NbrDiChannel		= 4,
		.i_NbrDoChannel		= 4,
		.i_DoMaxdata		= 1,
		.i_NbrTTLChannel	= 24,
		.b_AvailableConvertUnit	= 6,
		.ui_MinAcquisitiontimeNs = 5000,
		.interrupt		= v_APCI3XXX_Interrupt,
		.ai_config		= i_APCI3XXX_InsnConfigAnalogInput,
		.ai_read		= i_APCI3XXX_InsnReadAnalogInput,
		.ao_write		= i_APCI3XXX_InsnWriteAnalogOutput,
		.di_bits		= apci3xxx_di_insn_bits,
		.ttl_config		= i_APCI3XXX_InsnConfigInitTTLIO,
		.ttl_bits		= i_APCI3XXX_InsnBitsTTLIO,
		.ttl_read		= i_APCI3XXX_InsnReadTTLIO,
		.ttl_write		= i_APCI3XXX_InsnWriteTTLIO,
	},
	[BOARD_APCI3110_8_4] = {
		.pc_DriverName		= "apci3110-8-4",
		.i_IorangeBase1		= 256,
		.i_PCIEeprom		= ADDIDATA_NO_EEPROM,
		.pc_EepromChip		= ADDIDATA_9054,
		.i_NbrAiChannel		= 8,
		.i_NbrAiChannelDiff	= 4,
		.i_AiChannelList	= 8,
		.i_NbrAoChannel		= 4,
		.i_AiMaxdata		= 4095,
		.i_AoMaxdata		= 4095,
		.pr_AiRangelist		= &range_apci3XXX_ai,
		.pr_AoRangelist		= &range_apci3XXX_ao,
		.i_NbrDiChannel		= 4,
		.i_NbrDoChannel		= 4,
		.i_DoMaxdata		= 1,
		.i_NbrTTLChannel	= 24,
		.b_AvailableConvertUnit	= 6,
		.ui_MinAcquisitiontimeNs = 5000,
		.interrupt		= v_APCI3XXX_Interrupt,
		.ai_config		= i_APCI3XXX_InsnConfigAnalogInput,
		.ai_read		= i_APCI3XXX_InsnReadAnalogInput,
		.ao_write		= i_APCI3XXX_InsnWriteAnalogOutput,
		.di_bits		= apci3xxx_di_insn_bits,
		.ttl_config		= i_APCI3XXX_InsnConfigInitTTLIO,
		.ttl_bits		= i_APCI3XXX_InsnBitsTTLIO,
		.ttl_read		= i_APCI3XXX_InsnReadTTLIO,
		.ttl_write		= i_APCI3XXX_InsnWriteTTLIO,
	},
	[BOARD_APCI3116_16_4] = {
		.pc_DriverName		= "apci3116-16-4",
		.i_IorangeBase1		= 256,
		.i_PCIEeprom		= ADDIDATA_NO_EEPROM,
		.pc_EepromChip		= ADDIDATA_9054,
		.i_NbrAiChannel		= 16,
		.i_NbrAiChannelDiff	= 8,
		.i_AiChannelList	= 16,
		.i_NbrAoChannel		= 4,
		.i_AiMaxdata		= 65535,
		.i_AoMaxdata		= 4095,
		.pr_AiRangelist		= &range_apci3XXX_ai,
		.pr_AoRangelist		= &range_apci3XXX_ao,
		.i_NbrDiChannel		= 4,
		.i_NbrDoChannel		= 4,
		.i_DoMaxdata		= 1,
		.i_NbrTTLChannel	= 24,
		.b_AvailableConvertUnit	= 6,
		.ui_MinAcquisitiontimeNs = 5000,
		.interrupt		= v_APCI3XXX_Interrupt,
		.ai_config		= i_APCI3XXX_InsnConfigAnalogInput,
		.ai_read		= i_APCI3XXX_InsnReadAnalogInput,
		.ao_write		= i_APCI3XXX_InsnWriteAnalogOutput,
		.di_bits		= apci3xxx_di_insn_bits,
		.ttl_config		= i_APCI3XXX_InsnConfigInitTTLIO,
		.ttl_bits		= i_APCI3XXX_InsnBitsTTLIO,
		.ttl_read		= i_APCI3XXX_InsnReadTTLIO,
		.ttl_write		= i_APCI3XXX_InsnWriteTTLIO,
	},
	[BOARD_APCI3116_8_4] = {
		.pc_DriverName		= "apci3116-8-4",
		.i_IorangeBase1		= 256,
		.i_PCIEeprom		= ADDIDATA_NO_EEPROM,
		.pc_EepromChip		= ADDIDATA_9054,
		.i_NbrAiChannel		= 8,
		.i_NbrAiChannelDiff	= 4,
		.i_AiChannelList	= 8,
		.i_NbrAoChannel		= 4,
		.i_AiMaxdata		= 65535,
		.i_AoMaxdata		= 4095,
		.pr_AiRangelist		= &range_apci3XXX_ai,
		.pr_AoRangelist		= &range_apci3XXX_ao,
		.i_NbrDiChannel		= 4,
		.i_NbrDoChannel		= 4,
		.i_DoMaxdata		= 1,
		.i_NbrTTLChannel	= 24,
		.b_AvailableConvertUnit	= 6,
		.ui_MinAcquisitiontimeNs = 5000,
		.interrupt		= v_APCI3XXX_Interrupt,
		.ai_config		= i_APCI3XXX_InsnConfigAnalogInput,
		.ai_read		= i_APCI3XXX_InsnReadAnalogInput,
		.ao_write		= i_APCI3XXX_InsnWriteAnalogOutput,
		.di_bits		= apci3xxx_di_insn_bits,
		.ttl_config		= i_APCI3XXX_InsnConfigInitTTLIO,
		.ttl_bits		= i_APCI3XXX_InsnBitsTTLIO,
		.ttl_read		= i_APCI3XXX_InsnReadTTLIO,
		.ttl_write		= i_APCI3XXX_InsnWriteTTLIO,
	},
	[BOARD_APCI3003] = {
		.pc_DriverName		= "apci3003",
		.i_IorangeBase1		= 256,
		.i_PCIEeprom		= ADDIDATA_NO_EEPROM,
		.pc_EepromChip		= ADDIDATA_9054,
		.i_NbrAiChannelDiff	= 4,
		.i_AiChannelList	= 4,
		.i_AiMaxdata		= 65535,
		.pr_AiRangelist		= &range_apci3XXX_ai,
		.i_NbrDiChannel		= 4,
		.i_NbrDoChannel		= 4,
		.i_DoMaxdata		= 1,
		.b_AvailableConvertUnit	= 7,
		.ui_MinAcquisitiontimeNs = 2500,
		.interrupt		= v_APCI3XXX_Interrupt,
		.ai_config		= i_APCI3XXX_InsnConfigAnalogInput,
		.ai_read		= i_APCI3XXX_InsnReadAnalogInput,
		.di_bits		= apci3xxx_di_insn_bits,
	},
	[BOARD_APCI3002_16] = {
		.pc_DriverName		= "apci3002-16",
		.i_IorangeBase1		= 256,
		.i_PCIEeprom		= ADDIDATA_NO_EEPROM,
		.pc_EepromChip		= ADDIDATA_9054,
		.i_NbrAiChannelDiff	= 16,
		.i_AiChannelList	= 16,
		.i_AiMaxdata		= 65535,
		.pr_AiRangelist		= &range_apci3XXX_ai,
		.i_NbrDiChannel		= 4,
		.i_NbrDoChannel		= 4,
		.i_DoMaxdata		= 1,
		.b_AvailableConvertUnit	= 6,
		.ui_MinAcquisitiontimeNs = 5000,
		.interrupt		= v_APCI3XXX_Interrupt,
		.ai_config		= i_APCI3XXX_InsnConfigAnalogInput,
		.ai_read		= i_APCI3XXX_InsnReadAnalogInput,
		.di_bits		= apci3xxx_di_insn_bits,
	},
	[BOARD_APCI3002_8] = {
		.pc_DriverName		= "apci3002-8",
		.i_IorangeBase1		= 256,
		.i_PCIEeprom		= ADDIDATA_NO_EEPROM,
		.pc_EepromChip		= ADDIDATA_9054,
		.i_NbrAiChannelDiff	= 8,
		.i_AiChannelList	= 8,
		.i_AiMaxdata		= 65535,
		.pr_AiRangelist		= &range_apci3XXX_ai,
		.i_NbrDiChannel		= 4,
		.i_NbrDoChannel		= 4,
		.i_DoMaxdata		= 1,
		.b_AvailableConvertUnit	= 6,
		.ui_MinAcquisitiontimeNs = 5000,
		.interrupt		= v_APCI3XXX_Interrupt,
		.ai_config		= i_APCI3XXX_InsnConfigAnalogInput,
		.ai_read		= i_APCI3XXX_InsnReadAnalogInput,
		.di_bits		= apci3xxx_di_insn_bits,
	},
	[BOARD_APCI3002_4] = {
		.pc_DriverName		= "apci3002-4",
		.i_IorangeBase1		= 256,
		.i_PCIEeprom		= ADDIDATA_NO_EEPROM,
		.pc_EepromChip		= ADDIDATA_9054,
		.i_NbrAiChannelDiff	= 4,
		.i_AiChannelList	= 4,
		.i_AiMaxdata		= 65535,
		.pr_AiRangelist		= &range_apci3XXX_ai,
		.i_NbrDiChannel		= 4,
		.i_NbrDoChannel		= 4,
		.i_DoMaxdata		= 1,
		.b_AvailableConvertUnit	= 6,
		.ui_MinAcquisitiontimeNs = 5000,
		.interrupt		= v_APCI3XXX_Interrupt,
		.ai_config		= i_APCI3XXX_InsnConfigAnalogInput,
		.ai_read		= i_APCI3XXX_InsnReadAnalogInput,
		.di_bits		= apci3xxx_di_insn_bits,
	},
	[BOARD_APCI3500] = {
		.pc_DriverName		= "apci3500",
		.i_IorangeBase1		= 256,
		.i_PCIEeprom		= ADDIDATA_NO_EEPROM,
		.pc_EepromChip		= ADDIDATA_9054,
		.i_NbrAoChannel		= 4,
		.i_AoMaxdata		= 4095,
		.pr_AoRangelist		= &range_apci3XXX_ao,
		.i_NbrTTLChannel	= 24,
		.interrupt		= v_APCI3XXX_Interrupt,
		.ao_write		= i_APCI3XXX_InsnWriteAnalogOutput,
		.ttl_config		= i_APCI3XXX_InsnConfigInitTTLIO,
		.ttl_bits		= i_APCI3XXX_InsnBitsTTLIO,
		.ttl_read		= i_APCI3XXX_InsnReadTTLIO,
		.ttl_write		= i_APCI3XXX_InsnWriteTTLIO,
	},
};

static int apci3xxx_do_insn_bits(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	struct addi_private *devpriv = dev->private;
	unsigned int mask = data[0];
	unsigned int bits = data[1];

	s->state = inl(devpriv->iobase + 48) & 0xf;
	if (mask) {
		s->state &= ~mask;
		s->state |= (bits & mask);

		outl(s->state, devpriv->iobase + 48);
	}

	data[1] = s->state;

	return insn->n;
}

static int i_ADDIDATA_InsnReadEeprom(struct comedi_device *dev,
				     struct comedi_subdevice *s,
				     struct comedi_insn *insn,
				     unsigned int *data)
{
	const struct addi_board *this_board = comedi_board(dev);
	struct addi_private *devpriv = dev->private;
	unsigned short w_Address = CR_CHAN(insn->chanspec);
	unsigned short w_Data;

	w_Data = addi_eeprom_readw(devpriv->i_IobaseAmcc,
		this_board->pc_EepromChip, 2 * w_Address);
	data[0] = w_Data;

	return insn->n;
}

static irqreturn_t v_ADDI_Interrupt(int irq, void *d)
{
	struct comedi_device *dev = d;
	const struct addi_board *this_board = comedi_board(dev);

	this_board->interrupt(irq, d);
	return IRQ_RETVAL(1);
}

static int apci3xxx_reset(struct comedi_device *dev)
{
	struct addi_private *devpriv = dev->private;
	unsigned int val;
	int i;

	/* Disable the interrupt */
	disable_irq(dev->irq);

	/* Reset the interrupt flag */
	devpriv->b_EocEosInterrupt = 0;

	/* Clear the start command */
	writel(0, devpriv->dw_AiBase + 8);

	/* Reset the interrupt flags */
	val = readl(devpriv->dw_AiBase + 16);
	writel(val, devpriv->dw_AiBase + 16);

	/* clear the EOS */
	readl(devpriv->dw_AiBase + 20);

	/* Clear the FIFO */
	for (i = 0; i < 16; i++)
		val = readl(devpriv->dw_AiBase + 28);

	/* Enable the interrupt */
	enable_irq(dev->irq);

	return 0;
}

static int apci3xxx_auto_attach(struct comedi_device *dev,
				unsigned long context)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	const struct addi_board *board = NULL;
	struct addi_private *devpriv;
	struct comedi_subdevice *s;
	int ret, n_subdevices;
	unsigned int dw_Dummy;

	if (context < ARRAY_SIZE(apci3xxx_boardtypes))
		board = &apci3xxx_boardtypes[context];
	if (!board)
		return -ENODEV;
	dev->board_ptr = board;
	dev->board_name = board->pc_DriverName;

	devpriv = kzalloc(sizeof(*devpriv), GFP_KERNEL);
	if (!devpriv)
		return -ENOMEM;
	dev->private = devpriv;

	ret = comedi_pci_enable(dev);
	if (ret)
		return ret;

	if (!board->pc_EepromChip ||
	    strcmp(board->pc_EepromChip, ADDIDATA_9054)) {
		/* board does not have an eeprom or is not ADDIDATA_9054 */
		if (board->i_IorangeBase1)
			dev->iobase = pci_resource_start(pcidev, 1);
		else
			dev->iobase = pci_resource_start(pcidev, 0);

		devpriv->iobase = dev->iobase;
		devpriv->i_IobaseAmcc = pci_resource_start(pcidev, 0);
		devpriv->i_IobaseAddon = pci_resource_start(pcidev, 2);
	} else {
		/* board has an ADDIDATA_9054 eeprom */
		dev->iobase = pci_resource_start(pcidev, 2);
		devpriv->iobase = pci_resource_start(pcidev, 2);
		devpriv->dw_AiBase = pci_ioremap_bar(pcidev, 3);
	}
	devpriv->i_IobaseReserved = pci_resource_start(pcidev, 3);

	/* Initialize parameters that can be overridden in EEPROM */
	devpriv->s_EeParameters.i_NbrAiChannel = board->i_NbrAiChannel;
	devpriv->s_EeParameters.i_NbrAoChannel = board->i_NbrAoChannel;
	devpriv->s_EeParameters.i_AiMaxdata = board->i_AiMaxdata;
	devpriv->s_EeParameters.i_AoMaxdata = board->i_AoMaxdata;
	devpriv->s_EeParameters.i_NbrDiChannel = board->i_NbrDiChannel;
	devpriv->s_EeParameters.i_NbrDoChannel = board->i_NbrDoChannel;
	devpriv->s_EeParameters.i_DoMaxdata = board->i_DoMaxdata;
	devpriv->s_EeParameters.i_Dma = board->i_Dma;
	devpriv->s_EeParameters.i_Timer = board->i_Timer;
	devpriv->s_EeParameters.ui_MinAcquisitiontimeNs =
						board->ui_MinAcquisitiontimeNs;
	devpriv->s_EeParameters.ui_MinDelaytimeNs = board->ui_MinDelaytimeNs;

	/* ## */

	if (pcidev->irq > 0) {
		ret = request_irq(pcidev->irq, v_ADDI_Interrupt, IRQF_SHARED,
				  dev->board_name, dev);
		if (ret == 0)
			dev->irq = pcidev->irq;
	}

	/*  Read eepeom and fill addi_board Structure */

	if (board->i_PCIEeprom) {
		if (!(strcmp(board->pc_EepromChip, "S5920"))) {
			/*  Set 3 wait stait */
			if (!(strcmp(dev->board_name, "apci035")))
				outl(0x80808082, devpriv->i_IobaseAmcc + 0x60);
			else
				outl(0x83838383, devpriv->i_IobaseAmcc + 0x60);

			/*  Enable the interrupt for the controller */
			dw_Dummy = inl(devpriv->i_IobaseAmcc + 0x38);
			outl(dw_Dummy | 0x2000, devpriv->i_IobaseAmcc + 0x38);
		}
		addi_eeprom_read_info(dev, pci_resource_start(pcidev, 0));
	}

	n_subdevices = 7;
	ret = comedi_alloc_subdevices(dev, n_subdevices);
	if (ret)
		return ret;

	/*  Allocate and Initialise AI Subdevice Structures */
	s = &dev->subdevices[0];
	if (devpriv->s_EeParameters.i_NbrAiChannel ||
	    board->i_NbrAiChannelDiff) {
		dev->read_subdev = s;
		s->type = COMEDI_SUBD_AI;
		s->subdev_flags = SDF_READABLE | SDF_COMMON | SDF_GROUND |
				  SDF_DIFF;
		if (devpriv->s_EeParameters.i_NbrAiChannel) {
			s->n_chan = devpriv->s_EeParameters.i_NbrAiChannel;
			devpriv->b_SingelDiff = 0;
		} else {
			s->n_chan = board->i_NbrAiChannelDiff;
			devpriv->b_SingelDiff = 1;
		}
		s->maxdata = devpriv->s_EeParameters.i_AiMaxdata;
		s->len_chanlist = board->i_AiChannelList;
		s->range_table = board->pr_AiRangelist;

		/* Set the initialisation flag */
		devpriv->b_AiInitialisation = 1;

		s->insn_config = board->ai_config;
		s->insn_read = board->ai_read;
		s->insn_write = board->ai_write;
		s->insn_bits = board->ai_bits;
		s->do_cmdtest = board->ai_cmdtest;
		s->do_cmd = board->ai_cmd;
		s->cancel = board->ai_cancel;

	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}

	/*  Allocate and Initialise AO Subdevice Structures */
	s = &dev->subdevices[1];
	if (devpriv->s_EeParameters.i_NbrAoChannel) {
		s->type = COMEDI_SUBD_AO;
		s->subdev_flags = SDF_WRITEABLE | SDF_GROUND | SDF_COMMON;
		s->n_chan = devpriv->s_EeParameters.i_NbrAoChannel;
		s->maxdata = devpriv->s_EeParameters.i_AoMaxdata;
		s->len_chanlist = devpriv->s_EeParameters.i_NbrAoChannel;
		s->range_table = board->pr_AoRangelist;
		s->insn_config = board->ao_config;
		s->insn_write = board->ao_write;
	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}
	/*  Allocate and Initialise DI Subdevice Structures */
	s = &dev->subdevices[2];
	if (devpriv->s_EeParameters.i_NbrDiChannel) {
		s->type = COMEDI_SUBD_DI;
		s->subdev_flags = SDF_READABLE | SDF_GROUND | SDF_COMMON;
		s->n_chan = devpriv->s_EeParameters.i_NbrDiChannel;
		s->maxdata = 1;
		s->len_chanlist = devpriv->s_EeParameters.i_NbrDiChannel;
		s->range_table = &range_digital;
		s->io_bits = 0;	/* all bits input */
		s->insn_config = board->di_config;
		s->insn_read = board->di_read;
		s->insn_write = board->di_write;
		s->insn_bits = board->di_bits;
	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}
	/*  Allocate and Initialise DO Subdevice Structures */
	s = &dev->subdevices[3];
	if (devpriv->s_EeParameters.i_NbrDoChannel) {
		s->type = COMEDI_SUBD_DO;
		s->subdev_flags =
			SDF_READABLE | SDF_WRITEABLE | SDF_GROUND | SDF_COMMON;
		s->n_chan = devpriv->s_EeParameters.i_NbrDoChannel;
		s->maxdata = devpriv->s_EeParameters.i_DoMaxdata;
		s->len_chanlist = devpriv->s_EeParameters.i_NbrDoChannel;
		s->range_table = &range_digital;
		s->io_bits = 0xf;	/* all bits output */
		s->insn_bits = apci3xxx_do_insn_bits;
	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}

	/*  Allocate and Initialise Timer Subdevice Structures */
	s = &dev->subdevices[4];
	if (devpriv->s_EeParameters.i_Timer) {
		s->type = COMEDI_SUBD_TIMER;
		s->subdev_flags = SDF_WRITEABLE | SDF_GROUND | SDF_COMMON;
		s->n_chan = 1;
		s->maxdata = 0;
		s->len_chanlist = 1;
		s->range_table = &range_digital;

		s->insn_write = board->timer_write;
		s->insn_read = board->timer_read;
		s->insn_config = board->timer_config;
		s->insn_bits = board->timer_bits;
	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}

	/*  Allocate and Initialise TTL */
	s = &dev->subdevices[5];
	if (board->i_NbrTTLChannel) {
		s->type = COMEDI_SUBD_TTLIO;
		s->subdev_flags =
			SDF_WRITEABLE | SDF_READABLE | SDF_GROUND | SDF_COMMON;
		s->n_chan = board->i_NbrTTLChannel;
		s->maxdata = 1;
		s->io_bits = 0;	/* all bits input */
		s->len_chanlist = board->i_NbrTTLChannel;
		s->range_table = &range_digital;
		s->insn_config = board->ttl_config;
		s->insn_bits = board->ttl_bits;
		s->insn_read = board->ttl_read;
		s->insn_write = board->ttl_write;
	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}

	/* EEPROM */
	s = &dev->subdevices[6];
	if (board->i_PCIEeprom) {
		s->type = COMEDI_SUBD_MEMORY;
		s->subdev_flags = SDF_READABLE | SDF_INTERNAL;
		s->n_chan = 256;
		s->maxdata = 0xffff;
		s->insn_read = i_ADDIDATA_InsnReadEeprom;
	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}

	apci3xxx_reset(dev);
	return 0;
}

static void apci3xxx_detach(struct comedi_device *dev)
{
	struct addi_private *devpriv = dev->private;

	if (devpriv) {
		if (dev->iobase)
			apci3xxx_reset(dev);
		if (dev->irq)
			free_irq(dev->irq, dev);
		if (devpriv->dw_AiBase)
			iounmap(devpriv->dw_AiBase);
	}
	comedi_pci_disable(dev);
}

static struct comedi_driver apci3xxx_driver = {
	.driver_name	= "addi_apci_3xxx",
	.module		= THIS_MODULE,
	.auto_attach	= apci3xxx_auto_attach,
	.detach		= apci3xxx_detach,
};

static int apci3xxx_pci_probe(struct pci_dev *dev,
			      const struct pci_device_id *id)
{
	return comedi_pci_auto_config(dev, &apci3xxx_driver, id->driver_data);
}

static DEFINE_PCI_DEVICE_TABLE(apci3xxx_pci_table) = {
	{ PCI_VDEVICE(ADDIDATA, 0x3010), BOARD_APCI3000_16 },
	{ PCI_VDEVICE(ADDIDATA, 0x300f), BOARD_APCI3000_8 },
	{ PCI_VDEVICE(ADDIDATA, 0x300e), BOARD_APCI3000_4 },
	{ PCI_VDEVICE(ADDIDATA, 0x3013), BOARD_APCI3006_16 },
	{ PCI_VDEVICE(ADDIDATA, 0x3014), BOARD_APCI3006_8 },
	{ PCI_VDEVICE(ADDIDATA, 0x3015), BOARD_APCI3006_4 },
	{ PCI_VDEVICE(ADDIDATA, 0x3016), BOARD_APCI3010_16 },
	{ PCI_VDEVICE(ADDIDATA, 0x3017), BOARD_APCI3010_8 },
	{ PCI_VDEVICE(ADDIDATA, 0x3018), BOARD_APCI3010_4 },
	{ PCI_VDEVICE(ADDIDATA, 0x3019), BOARD_APCI3016_16 },
	{ PCI_VDEVICE(ADDIDATA, 0x301a), BOARD_APCI3016_8 },
	{ PCI_VDEVICE(ADDIDATA, 0x301b), BOARD_APCI3016_4 },
	{ PCI_VDEVICE(ADDIDATA, 0x301c), BOARD_APCI3100_16_4 },
	{ PCI_VDEVICE(ADDIDATA, 0x301d), BOARD_APCI3100_8_4 },
	{ PCI_VDEVICE(ADDIDATA, 0x301e), BOARD_APCI3106_16_4 },
	{ PCI_VDEVICE(ADDIDATA, 0x301f), BOARD_APCI3106_8_4 },
	{ PCI_VDEVICE(ADDIDATA, 0x3020), BOARD_APCI3110_16_4 },
	{ PCI_VDEVICE(ADDIDATA, 0x3021), BOARD_APCI3110_8_4 },
	{ PCI_VDEVICE(ADDIDATA, 0x3022), BOARD_APCI3116_16_4 },
	{ PCI_VDEVICE(ADDIDATA, 0x3023), BOARD_APCI3116_8_4 },
	{ PCI_VDEVICE(ADDIDATA, 0x300B), BOARD_APCI3003 },
	{ PCI_VDEVICE(ADDIDATA, 0x3002), BOARD_APCI3002_16 },
	{ PCI_VDEVICE(ADDIDATA, 0x3003), BOARD_APCI3002_8 },
	{ PCI_VDEVICE(ADDIDATA, 0x3004), BOARD_APCI3002_4 },
	{ PCI_VDEVICE(ADDIDATA, 0x3024), BOARD_APCI3500 },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, apci3xxx_pci_table);

static struct pci_driver apci3xxx_pci_driver = {
	.name		= "addi_apci_3xxx",
	.id_table	= apci3xxx_pci_table,
	.probe		= apci3xxx_pci_probe,
	.remove		= comedi_pci_auto_unconfig,
};
module_comedi_pci_driver(apci3xxx_driver, apci3xxx_pci_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
