/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 * The full GNU General Public License is included in this distribution
 * in the file called LICENSE.GPL.
 *
 * BSD LICENSE
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * This file defines all of the ATA related constants, enumerations, and types.
 *     Please note that this file does not necessarily contain an exhaustive
 *    list of all constants, commands, sub-commands, etc.
 *
 *
 */

#ifndef _ATA_H_
#define _ATA_H_

#include <linux/types.h>

/**
 *
 *
 * ATA_COMMAND_CODES These constants depict the various ATA command codes
 * defined in the ATA/ATAPI specification.
 */
#define ATA_IDENTIFY_DEVICE         0xEC
#define ATA_CHECK_POWER_MODE        0xE5
#define ATA_STANDBY                 0xE2
#define ATA_STANDBY_IMMED           0xE0
#define ATA_IDLE_IMMED              0xE1
#define ATA_IDLE                    0xE3
#define ATA_FLUSH_CACHE             0xE7
#define ATA_FLUSH_CACHE_EXT         0xEA
#define ATA_READ_DMA_EXT            0x25
#define ATA_READ_DMA                0xC8
#define ATA_READ_SECTORS_EXT        0x24
#define ATA_READ_SECTORS            0x20
#define ATA_WRITE_DMA_EXT           0x35
#define ATA_WRITE_DMA               0xCA
#define ATA_WRITE_SECTORS_EXT       0x34
#define ATA_WRITE_SECTORS           0x30
#define ATA_WRITE_UNCORRECTABLE     0x45
#define ATA_READ_VERIFY_SECTORS     0x40
#define ATA_READ_VERIFY_SECTORS_EXT 0x42
#define ATA_READ_BUFFER             0xE4
#define ATA_WRITE_BUFFER            0xE8
#define ATA_EXECUTE_DEVICE_DIAG     0x90
#define ATA_SET_FEATURES            0xEF
#define ATA_SMART                   0xB0
#define ATA_PACKET_IDENTIFY         0xA1
#define ATA_PACKET                  0xA0
#define ATA_READ_FPDMA              0x60
#define ATA_WRITE_FPDMA             0x61
#define ATA_READ_LOG_EXT            0x2F
#define ATA_NOP                     0x00
#define ATA_DEVICE_RESET            0x08
#define ATA_MEDIA_EJECT             0xED

/**
 *
 *
 * ATA_SMART_SUB_COMMAND_CODES These constants define the ATA SMART command
 * sub-codes that can be executed.
 */
#define ATA_SMART_SUB_CMD_ENABLE        0xD8
#define ATA_SMART_SUB_CMD_DISABLE       0xD9
#define ATA_SMART_SUB_CMD_RETURN_STATUS 0xDA
#define ATA_SMART_SUB_CMD_READ_LOG      0xD5

/**
 *
 *
 * ATA_SET_FEATURES_SUB_COMMAND_CODES These constants define the ATA SET
 * FEATURES command sub-codes that can be executed.
 */
#define ATA_SET_FEATURES_SUB_CMD_ENABLE_CACHE       0x02
#define ATA_SET_FEATURES_SUB_CMD_DISABLE_CACHE      0x82
#define ATA_SET_FEATURES_SUB_CMD_DISABLE_READ_AHEAD 0x55
#define ATA_SET_FEATURES_SUB_CMD_ENABLE_READ_AHEAD  0xAA
#define ATA_SET_FEATURES_SUB_CMD_SET_TRANSFER_MODE  0x3

/**
 *
 *
 * ATA_READ_LOG_EXT_PAGE_CODES This is a list of log page codes available for
 * use.
 */
#define ATA_LOG_PAGE_NCQ_ERROR                  0x10
#define ATA_LOG_PAGE_SMART_SELF_TEST            0x06
#define ATA_LOG_PAGE_EXTENDED_SMART_SELF_TEST   0x07

/**
 *
 *
 * ATA_LOG_PAGE_NCQ_ERROR_CONSTANTS These constants define standard values for
 * use when requesting the NCQ error log page.
 */
#define ATA_LOG_PAGE_NCQ_ERROR_SECTOR        0
#define ATA_LOG_PAGE_NCQ_ERROR_SECTOR_COUNT  1

/**
 *
 *
 * ATA_STATUS_REGISTER_BITS The following are status register bit definitions
 * per ATA/ATAPI-7.
 */
#define ATA_STATUS_REG_BSY_BIT          0x80
#define ATA_STATUS_REG_DEVICE_FAULT_BIT 0x20
#define ATA_STATUS_REG_ERROR_BIT        0x01

/**
 *
 *
 * ATA_ERROR_REGISTER_BITS The following are error register bit definitions per
 * ATA/ATAPI-7.
 */
#define ATA_ERROR_REG_NO_MEDIA_BIT              0x02
#define ATA_ERROR_REG_ABORT_BIT                 0x04
#define ATA_ERROR_REG_MEDIA_CHANGE_REQUEST_BIT  0x08
#define ATA_ERROR_REG_ID_NOT_FOUND_BIT          0x10
#define ATA_ERROR_REG_MEDIA_CHANGE_BIT          0x20
#define ATA_ERROR_REG_UNCORRECTABLE_BIT         0x40
#define ATA_ERROR_REG_WRITE_PROTECTED_BIT       0x40
#define ATA_ERROR_REG_ICRC_BIT                  0x80

/**
 *
 *
 * ATA_CONTROL_REGISTER_BITS The following are control register bit definitions
 * per ATA/ATAPI-7
 */
#define ATA_CONTROL_REG_INTERRUPT_ENABLE_BIT 0x02
#define ATA_CONTROL_REG_SOFT_RESET_BIT       0x04
#define ATA_CONTROL_REG_HIGH_ORDER_BYTE_BIT  0x80

/**
 *
 *
 * ATA_DEVICE_HEAD_REGISTER_BITS The following are device/head register bit
 * definitions per ATA/ATAPI-7.
 */
#define ATA_DEV_HEAD_REG_LBA_MODE_ENABLE  0x40
#define ATA_DEV_HEAD_REG_FUA_ENABLE       0x80

/**
 *
 *
 * ATA_IDENTIFY_DEVICE_FIELD_LENGTHS The following constants define the number
 * of bytes contained in various fields found in the IDENTIFY DEVICE data
 * structure.
 */
#define ATA_IDENTIFY_SERIAL_NUMBER_LEN        20
#define ATA_IDENTIFY_MODEL_NUMBER_LEN         40
#define ATA_IDENTIFY_FW_REVISION_LEN          8
#define ATA_IDENTIFY_48_LBA_LEN               8
#define ATA_IDENTIFY_MEDIA_SERIAL_NUMBER_LEN  30
#define ATA_IDENTIFY_WWN_LEN                  8

/**
 *
 *
 * ATA_IDENTIFY_DEVICE_FIELD_MASKS The following constants define bit masks
 * utilized to determine if a feature is supported/enabled or if a bit is
 * simply set inside of the IDENTIFY DEVICE data structre.
 */
#define ATA_IDENTIFY_REMOVABLE_MEDIA_ENABLE              0x0080
#define ATA_IDENTIFY_CAPABILITIES1_NORMAL_DMA_ENABLE     0x0100
#define ATA_IDENTIFY_CAPABILITIES1_STANDBY_ENABLE        0x2000
#define ATA_IDENTIFY_COMMAND_SET_SUPPORTED0_SMART_ENABLE 0x0001
#define ATA_IDENTIFY_COMMAND_SET_SUPPORTED1_48BIT_ENABLE 0x0400
#define ATA_IDENTIFY_COMMAND_SET_WWN_SUPPORT_ENABLE      0x0100
#define ATA_IDENTIFY_COMMAND_SET_ENABLED0_SMART_ENABLE   0x0001
#define ATA_IDENTIFY_SATA_CAPABILITIES_NCQ_ENABLE        0x0100
#define ATA_IDENTIFY_NCQ_QUEUE_DEPTH_ENABLE              0x001F
#define ATA_IDENTIFY_SECTOR_LARGER_THEN_512_ENABLE       0x0100
#define ATA_IDENTIFY_LOGICAL_SECTOR_PER_PHYSICAL_SECTOR_MASK   0x000F
#define ATA_IDENTIFY_LOGICAL_SECTOR_PER_PHYSICAL_SECTOR_ENABLE 0x2000
#define ATA_IDENTIFY_WRITE_UNCORRECTABLE_SUPPORT         0x0004
#define ATA_IDENTIFY_COMMAND_SET_SMART_SELF_TEST_SUPPORTED     0x0002

/**
 *
 *
 * ATAPI_IDENTIFY_DEVICE_FIELD_MASKS These constants define the various bit
 * definitions for the fields in the PACKET IDENTIFY DEVICE data structure.
 */
#define ATAPI_IDENTIFY_16BYTE_CMD_PCKT_ENABLE       0x01

/**
 *
 *
 * ATA_PACKET_FEATURE_BITS These constants define the various bit definitions
 * for the ATA PACKET feature register.
 */
#define ATA_PACKET_FEATURE_DMA     0x01
#define ATA_PACKET_FEATURE_OVL     0x02
#define ATA_PACKET_FEATURE_DMADIR  0x04

/**
 *
 *
 * ATA_Device_Power_Mode_Values These constants define the power mode values
 * returned by ATA_Check_Power_Mode
 */
#define ATA_STANDBY_POWER_MODE    0x00
#define ATA_IDLE_POWER_MODE       0x80
#define ATA_ACTIVE_POWER_MODE     0xFF

/**
 *
 *
 * ATA_WRITE_UNCORRECTIABLE feature field values These constants define the
 * Write Uncorrectable feature values used with the SATI translation.
 */
#define ATA_WRITE_UNCORRECTABLE_PSUEDO    0x55
#define ATA_WRITE_UNCORRECTABLE_FLAGGED   0xAA



/**
 * struct ATA_IDENTIFY_DEVICE - This structure depicts the ATA IDENTIFY DEVICE
 *    data format.
 *
 *
 */
struct ata_identify_device_data {
	u16 general_config_bits;                                                /* word  00 */
	u16 obsolete0;                                                          /* word  01 (num cylinders) */
	u16 vendor_specific_config_bits;                                        /* word  02 */
	u16 obsolete1;                                                          /* word  03 (num heads) */
	u16 retired1[2];                                                        /* words 04-05 */
	u16 obsolete2;                                                          /* word  06 (sectors / track) */
	u16 reserved_for_compact_flash1[2];                                     /* words 07-08 */
	u16 retired0;                                                           /* word  09 */
	u8 serial_number[ATA_IDENTIFY_SERIAL_NUMBER_LEN];                       /* word 10-19 */
	u16 retired2[2];                                                        /* words 20-21 */
	u16 obsolete4;                                                          /* word  22 */
	u8 firmware_revision[ATA_IDENTIFY_FW_REVISION_LEN];                     /* words 23-26 */
	u8 model_number[ATA_IDENTIFY_MODEL_NUMBER_LEN];                         /* words 27-46 */
	u16 max_sectors_per_multiple;                                           /* word  47 */
	u16 reserved0;                                                          /* word  48 */
	u16 capabilities1;                                                      /* word  49 */
	u16 capabilities2;                                                      /* word  50 */
	u16 obsolete5[2];                                                       /* words 51-52 */
	u16 validity_bits;                                                      /* word  53 */
	u16 obsolete6[5];                                                       /*
										 * words 54-58 Used to be:
										 * current cylinders,
										 * current heads,
										 * current sectors/Track,
										 * current capacity */
	u16 current_max_sectors_per_multiple;                                   /* word  59 */
	u8 total_num_sectors[4];                                                /* words 60-61 */
	u16 obsolete7;                                                          /* word  62 */
	u16 multi_word_dma_mode;                                                /* word  63 */
	u16 pio_modes_supported;                                                /* word  64 */
	u16 min_multiword_dma_transfer_cycle;                                   /* word  65 */
	u16 rec_min_multiword_dma_transfer_cycle;                               /* word  66 */
	u16 min_pio_transfer_no_flow_ctrl;                                      /* word  67 */
	u16 min_pio_transfer_with_flow_ctrl;                                    /* word  68 */
	u16 reserved1[2];                                                       /* words 69-70 */
	u16 reserved2[4];                                                       /* words 71-74 */
	u16 queue_depth;                                                        /* word  75 */
	u16 serial_ata_capabilities;                                            /* word  76 */
	u16 serial_ata_reserved;                                                /* word  77 */
	u16 serial_ata_features_supported;                                      /* word  78 */
	u16 serial_ata_features_enabled;                                        /* word  79 */
	u16 major_version_number;                                               /* word  80 */
	u16 minor_version_number;                                               /* word  81 */
	u16 command_set_supported0;                                             /* word  82 */
	u16 command_set_supported1;                                             /* word  83 */
	u16 command_set_supported_extention;                                    /* word  84 */
	u16 command_set_enabled0;                                               /* word  85 */
	u16 command_set_enabled1;                                               /* word  86 */
	u16 command_set_default;                                                /* word  87 */
	u16 ultra_dma_mode;                                                     /* word  88 */
	u16 security_erase_completion_time;                                     /* word  89 */
	u16 enhanced_security_erase_time;                                       /* word  90 */
	u16 current_power_mgmt_value;                                           /* word  91 */
	u16 master_password_revision;                                           /* word  92 */
	u16 hardware_reset_result;                                              /* word  93 */
	u16 current_acoustic_management_value;                                  /* word  94 */
	u16 stream_min_request_size;                                            /* word  95 */
	u16 stream_transfer_time;                                               /* word  96 */
	u16 stream_access_latency;                                              /* word  97 */
	u16 stream_performance_granularity[2];                                  /* words 98-99 */
	u8 max_48bit_lba[ATA_IDENTIFY_48_LBA_LEN];                              /* words 100-103 */
	u16 streaming_transfer_time;                                            /* word  104 */
	u16 reserved3;                                                          /* word  105 */
	u16 physical_logical_sector_info;                                       /* word  106 */
	u16 acoustic_test_interseek_delay;                                      /* word  107 */
	u8 world_wide_name[ATA_IDENTIFY_WWN_LEN];                               /* words 108-111 */
	u8 reserved_for_wwn_extention[ATA_IDENTIFY_WWN_LEN];                    /* words 112-115 */
	u16 reserved4;                                                          /* word  116 */
	u8 words_per_logical_sector[4];                                         /* words 117-118 */
	u16 command_set_supported2;                                             /* word  119 */
	u16 reserved5[7];                                                       /* words 120-126 */
	u16 removable_media_status;                                             /* word  127 */
	u16 security_status;                                                    /* word  128 */
	u16 vendor_specific1[31];                                               /* words 129-159 */
	u16 cfa_power_mode1;                                                    /* word  160 */
	u16 reserved_for_compact_flash2[7];                                     /* words 161-167 */
	u16 device_nominal_form_factor;                                         /* word 168 */
	u16 reserved_for_compact_flash3[7];                                     /* words 169-175 */
	u16 current_media_serial_number[ATA_IDENTIFY_MEDIA_SERIAL_NUMBER_LEN];  /* words 176-205 */
	u16 reserved6[3];                                                       /* words 206-208 */
	u16 logical_sector_alignment;                                           /* words 209 */
	u16 reserved7[7];                                                       /* words 210-216 */
	u16 nominal_media_rotation_rate;                                        /* word 217 */
	u16 reserved8[37];                                                      /* words 218-254 */
	u16 integrity_word;                                                     /* word  255 */

};

#define ATA_IDENTIFY_DEVICE_GET_OFFSET(field_name) \
	((unsigned long)&(((struct ata_identify_device_data *)0)->field_name))
#define ATA_IDENTIFY_DEVICE_WCE_ENABLE  0x20
#define ATA_IDENTIFY_DEVICE_RA_ENABLE   0x40

/**
 * struct ATAPI_IDENTIFY_PACKET_DATA - The following structure depicts the
 *    ATA-ATAPI 7 version of the IDENTIFY PACKET DEVICE data structure.
 *
 *
 */
struct atapi_identify_packet_device {
	u16 generalConfigBits;                                  /* word  00 */
	u16 reserved0;                                          /* word  01 (num cylinders) */
	u16 uniqueConfigBits;                                   /* word  02 */
	u16 reserved1[7];                                       /* words 03 - 09 */
	u8 serialNumber[ATA_IDENTIFY_SERIAL_NUMBER_LEN];        /* word 10-19 */
	u16 reserved2[3];                                       /* words 20-22 */
	u8 firmwareRevision[ATA_IDENTIFY_FW_REVISION_LEN];      /* words 23-26 */
	u8 modelNumber[ATA_IDENTIFY_MODEL_NUMBER_LEN];          /* words 27-46 */
	u16 reserved4[2];                                       /* words 47-48 */
	u16 capabilities1;                                      /* word  49 */
	u16 capabilities2;                                      /* word  50 */
	u16 obsolete0[2];                                       /* words 51-52 */
	u16 validityBits;                                       /* word  53 */
	u16 reserved[8];                                        /* words 54-61 */

	u16 DMADIRBitRequired;                                  /* word  62, page2 */
	u16 multiWordDmaMode;                                   /* word  63 */
	u16 pioModesSupported;                                  /* word  64 */
	u16 minMultiwordDmaTransferCycle;                       /* word  65 */
	u16 recMinMultiwordDmaTransferCycle;                    /* word  66 */
	u16 minPioTransferNoFlowCtrl;                           /* word  67 */
	u16 minPioTransferWithFlowCtrl;                         /* word  68 */
	u16 reserved6[2];                                       /* words 69-70 */
	u16 nsFromPACKETReceiptToBusRelease;                    /* word  71 */
	u16 nsFromSERVICEReceiptToBSYreset;                     /* wore  72 */
	u16 reserved7[2];                                       /* words 73-74 */
	u16 queueDepth;                                         /* word  75 */
	u16 serialAtaCapabilities;                              /* word  76 */
	u16 serialAtaReserved;                                  /* word  77 */
	u16 serialAtaFeaturesSupported;                         /* word  78 */
	u16 serialAtaFeaturesEnabled;                           /* word  79 */

	u16 majorVersionNumber;                                 /* word  80, page3 */
	u16 minorVersionNumber;                                 /* word  81 */
	u16 commandSetSupported0;                               /* word  82 */
	u16 commandSetSupported1;                               /* word  83 */

	u16 commandSetSupportedExtention;                       /* word  84, page4 */
	u16 commandSetEnabled0;                                 /* word  85 */
	u16 commandSetEnabled1;                                 /* word  86 */
	u16 commandSetDefault;                                  /* word  87 */

	u16 ultraDmaMode;                                       /* word  88, page5 */
	u16 reserved8[4];                                       /* words 89 - 92 */

	u16 hardwareResetResult;                                /* word  93, page6 */
	u16 currentAcousticManagementValue;                     /* word  94 */
	u16 reserved9[30];                                      /* words 95-124 */
	u16 ATAPIByteCount0Behavior;                            /* word  125 */
	u16 obsolete1;                                          /* word  126 */
	u16 removableMediaStatus;                               /* word  127, */

	u16 securityStatus;                                     /* word  128, page7 */
	u16 vendorSpecific1[31];                                /* words 129-159 */
	u16 reservedForCompactFlash[16];                        /* words 160-175 */
	u16 reserved10[79];                                     /* words 176-254 */
	u16 integrityWord;                                      /* word  255 */
};

/**
 * struct ata_extended_smart_self_test_log - The following structure depicts
 *    the ATA-8 version of the Extended SMART self test log page descriptor
 *    entry.
 *
 *
 */
union ata_descriptor_entry {
	struct DESCRIPTOR_ENTRY {
		u8 lba_field;
		u8 status_byte;
		u8 time_stamp_low;
		u8 time_stamp_high;
		u8 checkpoint_byte;
		u8 failing_lba_low;
		u8 failing_lba_mid;
		u8 failing_lba_high;
		u8 failing_lba_low_ext;
		u8 failing_lba_mid_ext;
		u8 failing_lba_high_ext;

		u8 vendor_specific1;
		u8 vendor_specific2;
		u8 vendor_specific3;
		u8 vendor_specific4;
		u8 vendor_specific5;
		u8 vendor_specific6;
		u8 vendor_specific7;
		u8 vendor_specific8;
		u8 vendor_specific9;
		u8 vendor_specific10;
		u8 vendor_specific11;
		u8 vendor_specific12;
		u8 vendor_specific13;
		u8 vendor_specific14;
		u8 vendor_specific15;
	} DESCRIPTOR_ENTRY;

	u8 descriptor_entry[26];

};

/**
 * struct ata_extended_smart_self_test_log - The following structure depicts
 *    the ATA-8 version of the SMART self test log page descriptor entry.
 *
 *
 */
union ata_smart_descriptor_entry {
	struct SMART_DESCRIPTOR_ENTRY {
		u8 lba_field;
		u8 status_byte;
		u8 time_stamp_low;
		u8 time_stamp_high;
		u8 checkpoint_byte;
		u8 failing_lba_low;
		u8 failing_lba_mid;
		u8 failing_lba_high;
		u8 failing_lba_low_ext;

		u8 vendor_specific1;
		u8 vendor_specific2;
		u8 vendor_specific3;
		u8 vendor_specific4;
		u8 vendor_specific5;
		u8 vendor_specific6;
		u8 vendor_specific7;
		u8 vendor_specific8;
		u8 vendor_specific9;
		u8 vendor_specific10;
		u8 vendor_specific11;
		u8 vendor_specific12;
		u8 vendor_specific13;
		u8 vendor_specific14;
		u8 vendor_specific15;
	} SMART_DESCRIPTOR_ENTRY;

	u8 smart_descriptor_entry[24];

};

/**
 * struct ata_extended_smart_self_test_log - The following structure depicts
 *    the ATA-8 version of the Extended SMART self test log page.
 *
 *
 */
struct ata_extended_smart_self_test_log {
	u8 self_test_log_data_structure_revision_number;        /* byte 0 */
	u8 reserved0;                                           /* byte 1 */
	u8 self_test_descriptor_index[2];                       /* byte 2-3 */

	union ata_descriptor_entry descriptor_entrys[19];           /* bytes 4-497 */

	u8 vendor_specific[2];                                  /* byte 498-499 */
	u8 reserved1[11];                                       /* byte 500-510 */
	u8 data_structure_checksum;                             /* byte 511 */

};

/**
 * struct ata_extended_smart_self_test_log - The following structure depicts
 *    the ATA-8 version of the SMART self test log page.
 *
 *
 */
struct ata_smart_self_test_log {
	u8 self_test_log_data_structure_revision_number[2];     /* bytes 0-1 */

	union ata_smart_descriptor_entry descriptor_entrys[21];     /* bytes 2-505 */

	u8 vendor_specific[2];                                  /* byte 506-507 */
	u8 self_test_index;                                     /* byte 508 */
	u8 reserved1[2];                                        /* byte 509-510 */
	u8 data_structure_checksum;                             /* byte 511 */

};

#endif /* _ATA_H_ */

