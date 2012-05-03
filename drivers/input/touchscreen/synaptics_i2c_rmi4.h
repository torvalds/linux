/**
 *
 * Synaptics Register Mapped Interface (RMI4) I2C Physical Layer Driver.
 * Copyright (c) 2007-2010, Synaptics Incorporated
 *
 * Author: Js HA <js.ha@stericsson.com> for ST-Ericsson
 * Author: Naveen Kumar G <naveen.gaddipati@stericsson.com> for ST-Ericsson
 * Copyright 2010 (c) ST-Ericsson AB
 */
/*
 * This file is licensed under the GPL2 license.
 *
 *#############################################################################
 * GPL
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 *#############################################################################
 */

#ifndef _SYNAPTICS_RMI4_H_INCLUDED_
#define _SYNAPTICS_RMI4_H_INCLUDED_


/*      Register Name                                      Address     Register Description */
/*      -------------                                      -------     -------------------- */
#define SYNA_F34_FLASH_DATA00                              0x0000   /* Block Number Low */
#define SYNA_F34_FLASH_DATA01                              0x0001   /* Block Number High */
#define SYNA_F34_FLASH_DATA02_00                           0x0002   /* Block Data 0 */
#define SYNA_F34_FLASH_DATA02_01                           0x0003   /* Block Data 1 */
#define SYNA_F34_FLASH_DATA02_02                           0x0004   /* Block Data 2 */
#define SYNA_F34_FLASH_DATA02_03                           0x0005   /* Block Data 3 */
#define SYNA_F34_FLASH_DATA02_04                           0x0006   /* Block Data 4 */
#define SYNA_F34_FLASH_DATA02_05                           0x0007   /* Block Data 5 */
#define SYNA_F34_FLASH_DATA02_06                           0x0008   /* Block Data 6 */
#define SYNA_F34_FLASH_DATA02_07                           0x0009   /* Block Data 7 */
#define SYNA_F34_FLASH_DATA02_08                           0x000A   /* Block Data 8 */
#define SYNA_F34_FLASH_DATA02_09                           0x000B   /* Block Data 9 */
#define SYNA_F34_FLASH_DATA02_10                           0x000C   /* Block Data 10 */
#define SYNA_F34_FLASH_DATA02_11                           0x000D   /* Block Data 11 */
#define SYNA_F34_FLASH_DATA02_12                           0x000E   /* Block Data 12 */
#define SYNA_F34_FLASH_DATA02_13                           0x000F   /* Block Data 13 */
#define SYNA_F34_FLASH_DATA02_14                           0x0010   /* Block Data 14 */
#define SYNA_F34_FLASH_DATA02_15                           0x0011   /* Block Data 15 */
#define SYNA_F34_FLASH_DATA03                              0x0012   /* Flash Control */
#define SYNA_F01_RMI_DATA00                                0x0013   /* Device Status */
#define SYNA_F01_RMI_DATA01_00                             0x0014   /* Interrupt Status */
#define SYNA_F11_2D_DATA00_00                              0x0015   /* 2D Finger State */
#define SYNA_F11_2D_DATA00_01                              0x0016   /* 2D Finger State */
#define SYNA_F11_2D_DATA01_00                              0x0017   /* 2D X Position (11:4) Finger 0 */
#define SYNA_F11_2D_DATA02_00                              0x0018   /* 2D Y Position (11:4) Finger 0 */
#define SYNA_F11_2D_DATA03_00                              0x0019   /* 2D Y/X Position (3:0) Finger 0 */
#define SYNA_F11_2D_DATA04_00                              0x001A   /* 2D Wy/Wx Finger 0 */
#define SYNA_F11_2D_DATA05_00                              0x001B   /* 2D Z Finger 0 */
#define SYNA_F11_2D_DATA01_01                              0x001C   /* 2D X Position (11:4) Finger 1 */
#define SYNA_F11_2D_DATA02_01                              0x001D   /* 2D Y Position (11:4) Finger 1 */
#define SYNA_F11_2D_DATA03_01                              0x001E   /* 2D Y/X Position (3:0) Finger 1 */
#define SYNA_F11_2D_DATA04_01                              0x001F   /* 2D Wy/Wx Finger 1 */
#define SYNA_F11_2D_DATA05_01                              0x0020   /* 2D Z Finger 1 */
#define SYNA_F11_2D_DATA01_02                              0x0021   /* 2D X Position (11:4) Finger 2 */
#define SYNA_F11_2D_DATA02_02                              0x0022   /* 2D Y Position (11:4) Finger 2 */
#define SYNA_F11_2D_DATA03_02                              0x0023   /* 2D Y/X Position (3:0) Finger 2 */
#define SYNA_F11_2D_DATA04_02                              0x0024   /* 2D Wy/Wx Finger 2 */
#define SYNA_F11_2D_DATA05_02                              0x0025   /* 2D Z Finger 2 */
#define SYNA_F11_2D_DATA01_03                              0x0026   /* 2D X Position (11:4) Finger 3 */
#define SYNA_F11_2D_DATA02_03                              0x0027   /* 2D Y Position (11:4) Finger 3 */
#define SYNA_F11_2D_DATA03_03                              0x0028   /* 2D Y/X Position (3:0) Finger 3 */
#define SYNA_F11_2D_DATA04_03                              0x0029   /* 2D Wy/Wx Finger 3 */
#define SYNA_F11_2D_DATA05_03                              0x002A   /* 2D Z Finger 3 */
#define SYNA_F11_2D_DATA01_04                              0x002B   /* 2D X Position (11:4) Finger 4 */
#define SYNA_F11_2D_DATA02_04                              0x002C   /* 2D Y Position (11:4) Finger 4 */
#define SYNA_F11_2D_DATA03_04                              0x002D   /* 2D Y/X Position (3:0) Finger 4 */
#define SYNA_F11_2D_DATA04_04                              0x002E   /* 2D Wy/Wx Finger 4 */
#define SYNA_F11_2D_DATA05_04                              0x002F   /* 2D Z Finger 4 */
#define SYNA_F01_RMI_CTRL00                                0x0030   /* Device Control */
#define SYNA_F01_RMI_CTRL01_00                             0x0031   /* Interrupt Enable 0 */
#define SYNA_F11_2D_CTRL00                                 0x0032   /* 2D Report Mode */
#define SYNA_F11_2D_CTRL01                                 0x0033   /* 2D Palm Detect */
#define SYNA_F11_2D_CTRL02                                 0x0034   /* 2D Delta-X Thresh */
#define SYNA_F11_2D_CTRL03                                 0x0035   /* 2D Delta-Y Thresh */
#define SYNA_F11_2D_CTRL04                                 0x0036   /* 2D Velocity */
#define SYNA_F11_2D_CTRL05                                 0x0037   /* 2D Acceleration */
#define SYNA_F11_2D_CTRL06                                 0x0038   /* 2D Max X Position (7:0) */
#define SYNA_F11_2D_CTRL07                                 0x0039   /* 2D Max X Position (11:8) */
#define SYNA_F11_2D_CTRL08                                 0x003A   /* 2D Max Y Position (7:0) */
#define SYNA_F11_2D_CTRL09                                 0x003B   /* 2D Max Y Position (11:8) */
#define SYNA_F11_2D_CTRL12_00                              0x003C   /* 2D Sensor Map 0 */
#define SYNA_F11_2D_CTRL12_01                              0x003D   /* 2D Sensor Map 1 */
#define SYNA_F11_2D_CTRL12_02                              0x003E   /* 2D Sensor Map 2 */
#define SYNA_F11_2D_CTRL12_03                              0x003F   /* 2D Sensor Map 3 */
#define SYNA_F11_2D_CTRL12_04                              0x0040   /* 2D Sensor Map 4 */
#define SYNA_F11_2D_CTRL12_05                              0x0041   /* 2D Sensor Map 5 */
#define SYNA_F11_2D_CTRL12_06                              0x0042   /* 2D Sensor Map 6 */
#define SYNA_F11_2D_CTRL12_07                              0x0043   /* 2D Sensor Map 7 */
#define SYNA_F11_2D_CTRL12_08                              0x0044   /* 2D Sensor Map 8 */
#define SYNA_F11_2D_CTRL12_09                              0x0045   /* 2D Sensor Map 9 */
#define SYNA_F11_2D_CTRL12_10                              0x0046   /* 2D Sensor Map 10 */
#define SYNA_F11_2D_CTRL12_11                              0x0047   /* 2D Sensor Map 11 */
#define SYNA_F11_2D_CTRL12_12                              0x0048   /* 2D Sensor Map 12 */
#define SYNA_F11_2D_CTRL12_13                              0x0049   /* 2D Sensor Map 13 */
#define SYNA_F11_2D_CTRL12_14                              0x004A   /* 2D Sensor Map 14 */
#define SYNA_F11_2D_CTRL12_15                              0x004B   /* 2D Sensor Map 15 */
#define SYNA_F11_2D_CTRL12_16                              0x004C   /* 2D Sensor Map 16 */
#define SYNA_F11_2D_CTRL12_17                              0x004D   /* 2D Sensor Map 17 */
#define SYNA_F11_2D_CTRL12_18                              0x004E   /* 2D Sensor Map 18 */
#define SYNA_F11_2D_CTRL12_19                              0x004F   /* 2D Sensor Map 19 */
#define SYNA_F11_2D_CTRL12_20                              0x0050   /* 2D Sensor Map 20 */
#define SYNA_F11_2D_CTRL12_21                              0x0051   /* 2D Sensor Map 21 */
#define SYNA_F11_2D_CTRL12_22                              0x0052   /* 2D Sensor Map 22 */
#define SYNA_F11_2D_CTRL12_23                              0x0053   /* 2D Sensor Map 23 */
#define SYNA_F11_2D_CTRL12_24                              0x0054   /* 2D Sensor Map 24 */
#define SYNA_F11_2D_CTRL12_25                              0x0055   /* 2D Sensor Map 25 */
#define SYNA_F11_2D_CTRL12_26                              0x0056   /* 2D Sensor Map 26 */
#define SYNA_F11_2D_CTRL12_27                              0x0057   /* 2D Sensor Map 27 */
#define SYNA_F11_2D_CTRL12_28                              0x0058   /* 2D Sensor Map 28 */
#define SYNA_F11_2D_CTRL12_29                              0x0059   /* 2D Sensor Map 29 */
#define SYNA_F11_2D_CTRL12_30                              0x005A   /* 2D Sensor Map 30 */
#define SYNA_F11_2D_CTRL12_31                              0x005B   /* 2D Sensor Map 31 */
#define SYNA_F11_2D_CTRL12_32                              0x005C   /* 2D Sensor Map 32 */
#define SYNA_F11_2D_CTRL12_33                              0x005D   /* 2D Sensor Map 33 */
#define SYNA_F11_2D_CTRL12_34                              0x005E   /* 2D Sensor Map 34 */
#define SYNA_F11_2D_CTRL12_35                              0x005F   /* 2D Sensor Map 35 */
#define SYNA_F11_2D_CTRL12_36                              0x0060   /* 2D Sensor Map 36 */
#define SYNA_F11_2D_CTRL12_37                              0x0061   /* 2D Sensor Map 37 */
#define SYNA_F11_2D_CTRL12_38                              0x0062   /* 2D Sensor Map 38 */
#define SYNA_F11_2D_CTRL12_39                              0x0063   /* 2D Sensor Map 39 */
#define SYNA_F11_2D_CTRL12_40                              0x0064   /* 2D Sensor Map 40 */
#define SYNA_F11_2D_CTRL12_41                              0x0065   /* 2D Sensor Map 41 */
#define SYNA_F11_2D_CTRL12_42                              0x0066   /* 2D Sensor Map 42 */
#define SYNA_F11_2D_CTRL12_43                              0x0067   /* 2D Sensor Map 43 */
#define SYNA_F11_2D_CTRL12_44                              0x0068   /* 2D Sensor Map 44 */
#define SYNA_F11_2D_CTRL14                                 0x0096   /* 2D Sensitivity Adjustment */
#define SYNA_F01_RMI_CMD00                                 0x0097   /* Device Command */
#define SYNA_F11_2D_CMD00                                  0x0098   /* 2D Command */
#define SYNA_F34_FLASH_QUERY00                             0x0099   /* Bootloader ID 0 */
#define SYNA_F34_FLASH_QUERY01                             0x009A   /* Bootloader ID 1 */
#define SYNA_F34_FLASH_QUERY02                             0x009B   /* Flash Properties */
#define SYNA_F34_FLASH_QUERY03                             0x009C   /* Block Size 0 */
#define SYNA_F34_FLASH_QUERY04                             0x009D   /* Block Size 1 */
#define SYNA_F34_FLASH_QUERY05                             0x009E   /* Firmware Block Count 0 */
#define SYNA_F34_FLASH_QUERY06                             0x009F   /* Firmware Block Count 1 */
#define SYNA_F34_FLASH_QUERY07                             0x00A0   /* Configuration Block Count 0 */
#define SYNA_F34_FLASH_QUERY08                             0x00A1   /* Configuration Block Count 1 */
#define SYNA_F01_RMI_QUERY00                               0x00A2   /* Manufacturer ID Query */
#define SYNA_F01_RMI_QUERY01                               0x00A3   /* Product Properties Query */
#define SYNA_F01_RMI_QUERY02                               0x00A4   /* Customer Family Query */
#define SYNA_F01_RMI_QUERY03                               0x00A5   /* Firmware Revision Query */
#define SYNA_F01_RMI_QUERY04                               0x00A6   /* Device Serialization Query 0 */
#define SYNA_F01_RMI_QUERY05                               0x00A7   /* Device Serialization Query 1 */
#define SYNA_F01_RMI_QUERY06                               0x00A8   /* Device Serialization Query 2 */
#define SYNA_F01_RMI_QUERY07                               0x00A9   /* Device Serialization Query 3 */
#define SYNA_F01_RMI_QUERY08                               0x00AA   /* Device Serialization Query 4 */
#define SYNA_F01_RMI_QUERY09                               0x00AB   /* Device Serialization Query 5 */
#define SYNA_F01_RMI_QUERY10                               0x00AC   /* Device Serialization Query 6 */
#define SYNA_F01_RMI_QUERY11                               0x00AD   /* Product ID Query 0 */
#define SYNA_F01_RMI_QUERY12                               0x00AE   /* Product ID Query 1 */
#define SYNA_F01_RMI_QUERY13                               0x00AF   /* Product ID Query 2 */
#define SYNA_F01_RMI_QUERY14                               0x00B0   /* Product ID Query 3 */
#define SYNA_F01_RMI_QUERY15                               0x00B1   /* Product ID Query 4 */
#define SYNA_F01_RMI_QUERY16                               0x00B2   /* Product ID Query 5 */
#define SYNA_F01_RMI_QUERY17                               0x00B3   /* Product ID Query 6 */
#define SYNA_F01_RMI_QUERY18                               0x00B4   /* Product ID Query 7 */
#define SYNA_F01_RMI_QUERY19                               0x00B5   /* Product ID Query 8 */
#define SYNA_F01_RMI_QUERY20                               0x00B6   /* Product ID Query 9 */
#define SYNA_F11_2D_QUERY00                                0x00B7   /* Per-device Query */
#define SYNA_F11_2D_QUERY01                                0x00B8   /* 2D Reporting Mode */
#define SYNA_F11_2D_QUERY02                                0x00B9   /* 2D Number of X Electrodes */
#define SYNA_F11_2D_QUERY03                                0x00BA   /* 2D Number of Y Electrodes */
#define SYNA_F11_2D_QUERY04                                0x00BB   /* 2D Maximum Electrodes */
#define SYNA_F11_2D_QUERY05                                0x00BC   /* 2D Absolute Query */

/* Start of Page Description Table (PDT) */

#define SYNA_PDT_P00_F11_2D_QUERY_BASE                     0x00DD   /* Query Base */
#define SYNA_PDT_P00_F11_2D_COMMAND_BASE                   0x00DE   /* Command Base */
#define SYNA_PDT_P00_F11_2D_CONTROL_BASE                   0x00DF   /* Control Base */
#define SYNA_PDT_P00_F11_2D_DATA_BASE                      0x00E0   /* Data Base */
#define SYNA_PDT_P00_F11_2D_INTERRUPTS                     0x00E1   /* Interrupt Source Count */
#define SYNA_PDT_P00_F11_2D_EXISTS                         0x00E2   /* Function Exists */
#define SYNA_PDT_P00_F01_RMI_QUERY_BASE                    0x00E3   /* Query Base */
#define SYNA_PDT_P00_F01_RMI_COMMAND_BASE                  0x00E4   /* Command Base */
#define SYNA_PDT_P00_F01_RMI_CONTROL_BASE                  0x00E5   /* Control Base */
#define SYNA_PDT_P00_F01_RMI_DATA_BASE                     0x00E6   /* Data Base */
#define SYNA_PDT_P00_F01_RMI_INTERRUPTS                    0x00E7   /* Interrupt Source Count */
#define SYNA_PDT_P00_F01_RMI_EXISTS                        0x00E8   /* Function Exists */
#define SYNA_PDT_P00_F34_FLASH_QUERY_BASE                  0x00E9   /* Query Base */
#define SYNA_PDT_P00_F34_FLASH_COMMAND_BASE                0x00EA   /* Command Base */
#define SYNA_PDT_P00_F34_FLASH_CONTROL_BASE                0x00EB   /* Control Base */
#define SYNA_PDT_P00_F34_FLASH_DATA_BASE                   0x00EC   /* Data Base */
#define SYNA_PDT_P00_F34_FLASH_INTERRUPTS                  0x00ED   /* Interrupt Source Count */
#define SYNA_PDT_P00_F34_FLASH_EXISTS                      0x00EE   /* Function Exists */
#define SYNA_P00_PDT_PROPERTIES                            0x00EF   /* P00_PDT Properties */
#define SYNA_P00_PAGESELECT                                0x00FF   /* Page Select register */

/* Registers on Page 0x01 */

/*      Register Name                                      Address     Register Description */
/*      -------------                                      -------     -------------------- */
#define SYNA_F05_ANALOG_DATA00                             0x0100   /* Reserved */
#define SYNA_F05_ANALOG_DATA01                             0x0101   /* Report Mode and Index */
#define SYNA_F05_ANALOG_DATA02_00                          0x0102   /* Report Data Window 0 */
#define SYNA_F05_ANALOG_DATA02_01                          0x0103   /* Report Data Window 1 */
#define SYNA_F05_ANALOG_DATA02_02                          0x0104   /* Report Data Window 2 */
#define SYNA_F05_ANALOG_DATA02_03                          0x0105   /* Report Data Window 3 */
#define SYNA_F05_ANALOG_DATA02_04                          0x0106   /* Report Data Window 4 */
#define SYNA_F05_ANALOG_DATA02_05                          0x0107   /* Report Data Window 5 */
#define SYNA_F05_ANALOG_DATA02_06                          0x0108   /* Report Data Window 6 */
#define SYNA_F05_ANALOG_DATA02_07                          0x0109   /* Report Data Window 7 */
#define SYNA_F05_ANALOG_DATA02_08                          0x010A   /* Report Data Window 8 */
#define SYNA_F05_ANALOG_DATA02_09                          0x010B   /* Report Data Window 9 */
#define SYNA_F05_ANALOG_DATA02_10                          0x010C   /* Report Data Window 10 */
#define SYNA_F05_ANALOG_DATA02_11                          0x010D   /* Report Data Window 11 */
#define SYNA_F05_ANALOG_DATA02_12                          0x010E   /* Report Data Window 12 */
#define SYNA_F05_ANALOG_DATA02_13                          0x010F   /* Report Data Window 13 */
#define SYNA_F05_ANALOG_DATA02_14                          0x0110   /* Report Data Window 14 */
#define SYNA_F05_ANALOG_DATA02_15                          0x0111   /* Report Data Window 15 */
#define SYNA_F05_ANALOG_DATA02_16                          0x0112   /* Report Data Window 16 */
#define SYNA_F05_ANALOG_DATA02_17                          0x0113   /* Report Data Window 17 */
#define SYNA_F05_ANALOG_DATA02_18                          0x0114   /* Report Data Window 18 */
#define SYNA_F05_ANALOG_DATA02_19                          0x0115   /* Report Data Window 19 */
#define SYNA_F05_ANALOG_DATA02_20                          0x0116   /* Report Data Window 20 */
#define SYNA_F05_ANALOG_DATA02_21                          0x0117   /* Report Data Window 21 */
#define SYNA_F05_ANALOG_DATA02_22                          0x0118   /* Report Data Window 22 */
#define SYNA_F05_ANALOG_DATA02_23                          0x0119   /* Report Data Window 23 */
#define SYNA_F05_ANALOG_DATA02_24                          0x011A   /* Report Data Window 24 */
#define SYNA_F05_ANALOG_DATA02_25                          0x011B   /* Report Data Window 25 */
#define SYNA_F05_ANALOG_DATA02_26                          0x011C   /* Report Data Window 26 */
#define SYNA_F05_ANALOG_DATA02_27                          0x011D   /* Report Data Window 27 */
#define SYNA_F05_ANALOG_DATA02_28                          0x011E   /* Report Data Window 28 */
#define SYNA_F05_ANALOG_DATA02_29                          0x011F   /* Report Data Window 29 */
#define SYNA_F05_ANALOG_DATA02_30                          0x0120   /* Report Data Window 30 */
#define SYNA_F05_ANALOG_DATA02_31                          0x0121   /* Report Data Window 31 */
#define SYNA_F05_ANALOG_DATA02_32                          0x0122   /* Report Data Window 32 */
#define SYNA_F05_ANALOG_DATA02_33                          0x0123   /* Report Data Window 33 */
#define SYNA_F05_ANALOG_CTRL00                             0x0124   /* Algo Control */
#define SYNA_F05_ANALOG_CTRL01                             0x0125   /* Reserved */
#define SYNA_F05_ANALOG_CTRL02                             0x0126   /* Reserved */
#define SYNA_F05_ANALOG_CTRL03                             0x0127   /* Reserved */
#define SYNA_F05_ANALOG_CTRL04                             0x0128   /* Reserved */
#define SYNA_F05_ANALOG_CTRL05                             0x0129   /* Reserved */
#define SYNA_F05_ANALOG_CMD00                              0x012A   /* Analog Command */
#define SYNA_F05_ANALOG_QUERY00                            0x012B   /* Receiver Electrodes */
#define SYNA_F05_ANALOG_QUERY01                            0x012C   /* Transmitter Electrodes */
#define SYNA_F05_ANALOG_QUERY02                            0x012D   /* Reserved */
#define SYNA_F05_ANALOG_QUERY03                            0x012E   /* Features */
#define SYNA_F05_ANALOG_QUERY04                            0x012F   /* Image Window Count */
#define SYNA_F05_ANALOG_QUERY05                            0x0130   /* Reserved */

/* Start of Page Description Table (PDT) for Page 0x01 */

#define SYNA_PDT_P01_F05_ANALOG_QUERY_BASE                 0x01E9   /* Query Base */
#define SYNA_PDT_P01_F05_ANALOG_COMMAND_BASE               0x01EA   /* Command Base */
#define SYNA_PDT_P01_F05_ANALOG_CONTROL_BASE               0x01EB   /* Control Base */
#define SYNA_PDT_P01_F05_ANALOG_DATA_BASE                  0x01EC   /* Data Base */
#define SYNA_PDT_P01_F05_ANALOG_INTERRUPTS                 0x01ED   /* Interrupt Source Count */
#define SYNA_PDT_P01_F05_ANALOG_EXISTS                     0x01EE   /* Function Exists */
#define SYNA_P01_PDT_PROPERTIES                            0x01EF   /* P01_PDT Properties */
#define SYNA_P01_PAGESELECT                                0x01FF   /* Page Select register */

/* Offsets within the configuration block */

/*      Register Name                                      Offset      Register Description */
/*      -------------                                      ------      -------------------- */
#define SYNA_F01_RMI_CTRL00_CFGBLK_OFS                     0x0000   /* Device Control */
#define SYNA_F01_RMI_CTRL01_00_CFGBLK_OFS                  0x0001   /* Interrupt Enable 0 */
#define SYNA_F11_2D_CTRL00_CFGBLK_OFS                      0x0002   /* 2D Report Mode */
#define SYNA_F11_2D_CTRL01_CFGBLK_OFS                      0x0003   /* 2D Palm Detect */
#define SYNA_F11_2D_CTRL02_CFGBLK_OFS                      0x0004   /* 2D Delta-X Thresh */
#define SYNA_F11_2D_CTRL03_CFGBLK_OFS                      0x0005   /* 2D Delta-Y Thresh */
#define SYNA_F11_2D_CTRL04_CFGBLK_OFS                      0x0006   /* 2D Velocity */
#define SYNA_F11_2D_CTRL05_CFGBLK_OFS                      0x0007   /* 2D Acceleration */
#define SYNA_F11_2D_CTRL06_CFGBLK_OFS                      0x0008   /* 2D Max X Position (7:0) */
#define SYNA_F11_2D_CTRL07_CFGBLK_OFS                      0x0009   /* 2D Max X Position (11:8) */
#define SYNA_F11_2D_CTRL08_CFGBLK_OFS                      0x000A   /* 2D Max Y Position (7:0) */
#define SYNA_F11_2D_CTRL09_CFGBLK_OFS                      0x000B   /* 2D Max Y Position (11:8) */
#define SYNA_F11_2D_CTRL12_00_CFGBLK_OFS                   0x000C   /* 2D Sensor Map 0 */
#define SYNA_F11_2D_CTRL12_01_CFGBLK_OFS                   0x000D   /* 2D Sensor Map 1 */
#define SYNA_F11_2D_CTRL12_02_CFGBLK_OFS                   0x000E   /* 2D Sensor Map 2 */
#define SYNA_F11_2D_CTRL12_03_CFGBLK_OFS                   0x000F   /* 2D Sensor Map 3 */
#define SYNA_F11_2D_CTRL12_04_CFGBLK_OFS                   0x0010   /* 2D Sensor Map 4 */
#define SYNA_F11_2D_CTRL12_05_CFGBLK_OFS                   0x0011   /* 2D Sensor Map 5 */
#define SYNA_F11_2D_CTRL12_06_CFGBLK_OFS                   0x0012   /* 2D Sensor Map 6 */
#define SYNA_F11_2D_CTRL12_07_CFGBLK_OFS                   0x0013   /* 2D Sensor Map 7 */
#define SYNA_F11_2D_CTRL12_08_CFGBLK_OFS                   0x0014   /* 2D Sensor Map 8 */
#define SYNA_F11_2D_CTRL12_09_CFGBLK_OFS                   0x0015   /* 2D Sensor Map 9 */
#define SYNA_F11_2D_CTRL12_10_CFGBLK_OFS                   0x0016   /* 2D Sensor Map 10 */
#define SYNA_F11_2D_CTRL12_11_CFGBLK_OFS                   0x0017   /* 2D Sensor Map 11 */
#define SYNA_F11_2D_CTRL12_12_CFGBLK_OFS                   0x0018   /* 2D Sensor Map 12 */
#define SYNA_F11_2D_CTRL12_13_CFGBLK_OFS                   0x0019   /* 2D Sensor Map 13 */
#define SYNA_F11_2D_CTRL12_14_CFGBLK_OFS                   0x001A   /* 2D Sensor Map 14 */
#define SYNA_F11_2D_CTRL12_15_CFGBLK_OFS                   0x001B   /* 2D Sensor Map 15 */
#define SYNA_F11_2D_CTRL12_16_CFGBLK_OFS                   0x001C   /* 2D Sensor Map 16 */
#define SYNA_F11_2D_CTRL12_17_CFGBLK_OFS                   0x001D   /* 2D Sensor Map 17 */
#define SYNA_F11_2D_CTRL12_18_CFGBLK_OFS                   0x001E   /* 2D Sensor Map 18 */
#define SYNA_F11_2D_CTRL12_19_CFGBLK_OFS                   0x001F   /* 2D Sensor Map 19 */
#define SYNA_F11_2D_CTRL12_20_CFGBLK_OFS                   0x0020   /* 2D Sensor Map 20 */
#define SYNA_F11_2D_CTRL12_21_CFGBLK_OFS                   0x0021   /* 2D Sensor Map 21 */
#define SYNA_F11_2D_CTRL12_22_CFGBLK_OFS                   0x0022   /* 2D Sensor Map 22 */
#define SYNA_F11_2D_CTRL12_23_CFGBLK_OFS                   0x0023   /* 2D Sensor Map 23 */
#define SYNA_F11_2D_CTRL12_24_CFGBLK_OFS                   0x0024   /* 2D Sensor Map 24 */
#define SYNA_F11_2D_CTRL12_25_CFGBLK_OFS                   0x0025   /* 2D Sensor Map 25 */
#define SYNA_F11_2D_CTRL12_26_CFGBLK_OFS                   0x0026   /* 2D Sensor Map 26 */
#define SYNA_F11_2D_CTRL12_27_CFGBLK_OFS                   0x0027   /* 2D Sensor Map 27 */
#define SYNA_F11_2D_CTRL12_28_CFGBLK_OFS                   0x0028   /* 2D Sensor Map 28 */
#define SYNA_F11_2D_CTRL12_29_CFGBLK_OFS                   0x0029   /* 2D Sensor Map 29 */
#define SYNA_F11_2D_CTRL12_30_CFGBLK_OFS                   0x002A   /* 2D Sensor Map 30 */
#define SYNA_F11_2D_CTRL12_31_CFGBLK_OFS                   0x002B   /* 2D Sensor Map 31 */
#define SYNA_F11_2D_CTRL12_32_CFGBLK_OFS                   0x002C   /* 2D Sensor Map 32 */
#define SYNA_F11_2D_CTRL12_33_CFGBLK_OFS                   0x002D   /* 2D Sensor Map 33 */
#define SYNA_F11_2D_CTRL12_34_CFGBLK_OFS                   0x002E   /* 2D Sensor Map 34 */
#define SYNA_F11_2D_CTRL12_35_CFGBLK_OFS                   0x002F   /* 2D Sensor Map 35 */
#define SYNA_F11_2D_CTRL12_36_CFGBLK_OFS                   0x0030   /* 2D Sensor Map 36 */
#define SYNA_F11_2D_CTRL12_37_CFGBLK_OFS                   0x0031   /* 2D Sensor Map 37 */
#define SYNA_F11_2D_CTRL12_38_CFGBLK_OFS                   0x0032   /* 2D Sensor Map 38 */
#define SYNA_F11_2D_CTRL12_39_CFGBLK_OFS                   0x0033   /* 2D Sensor Map 39 */
#define SYNA_F11_2D_CTRL12_40_CFGBLK_OFS                   0x0034   /* 2D Sensor Map 40 */
#define SYNA_F11_2D_CTRL12_41_CFGBLK_OFS                   0x0035   /* 2D Sensor Map 41 */
#define SYNA_F11_2D_CTRL12_42_CFGBLK_OFS                   0x0036   /* 2D Sensor Map 42 */
#define SYNA_F11_2D_CTRL12_43_CFGBLK_OFS                   0x0037   /* 2D Sensor Map 43 */
#define SYNA_F11_2D_CTRL12_44_CFGBLK_OFS                   0x0038   /* 2D Sensor Map 44 */
#define SYNA_F11_2D_CTRL14_CFGBLK_OFS                      0x0066   /* 2D Sensitivity Adjustment */
#define SYNA_F05_ANALOG_CTRL00_CFGBLK_OFS                  0x0067   /* Algo Control */
#define SYNA_F05_ANALOG_CTRL01_CFGBLK_OFS                  0x0068   /* Reserved */
#define SYNA_F05_ANALOG_CTRL02_CFGBLK_OFS                  0x0069   /* Reserved */
#define SYNA_F05_ANALOG_CTRL03_CFGBLK_OFS                  0x006A   /* Reserved */
#define SYNA_F05_ANALOG_CTRL04_CFGBLK_OFS                  0x006B   /* Reserved */
#define SYNA_F05_ANALOG_CTRL05_CFGBLK_OFS                  0x006C   /* Reserved */
#define SYNA_CFGBLK_CRC1_CFGBLK_OFS                        0x01FC   /* Configuration CRC [7:0] */
#define SYNA_CFGBLK_CRC2_CFGBLK_OFS                        0x01FD   /* Configuration CRC [15:8] */
#define SYNA_CFGBLK_CRC3_CFGBLK_OFS                        0x01FE   /* Configuration CRC [23:16] */
#define SYNA_CFGBLK_CRC4_CFGBLK_OFS                        0x01FF   /* Configuration CRC [31:24] */

/* Masks for interrupt sources */

/*      Symbol Name                                        Mask        Description */
/*      -----------                                        ----        ----------- */
#define SYNA_F01_RMI_INT_SOURCE_MASK_ALL                   0x0002   /* Mask of all Func $01 (RMI) interrupts */
#define SYNA_F01_RMI_INT_SOURCE_MASK_STATUS                0x0002   /* Mask of Func $01 (RMI) 'STATUS' interrupt */
#define SYNA_F05_ANALOG_INT_SOURCE_MASK_ALL                0x0008   /* Mask of all Func $05 (ANALOG) interrupts */
#define SYNA_F05_ANALOG_INT_SOURCE_MASK_ANALOG             0x0008   /* Mask of Func $05 (ANALOG) 'ANALOG' interrupt */
#define SYNA_F11_2D_INT_SOURCE_MASK_ABS0                   0x0004   /* Mask of Func $11 (2D) 'ABS0' interrupt */
#define SYNA_F11_2D_INT_SOURCE_MASK_ALL                    0x0004   /* Mask of all Func $11 (2D) interrupts */
#define SYNA_F34_FLASH_INT_SOURCE_MASK_ALL                 0x0001   /* Mask of all Func $34 (FLASH) interrupts */
#define SYNA_F34_FLASH_INT_SOURCE_MASK_FLASH               0x0001   /* Mask of Func $34 (FLASH) 'FLASH' interrupt */

/* cwz: change to arch/board.h */
#if 1
/**
 * struct synaptics_rmi4_platform_data - contains the rmi4 platform data
 * @irq_number: irq number
 * @irq_type: irq type
 * @x flip: x flip flag
 * @y flip: y flip flag
 * @regulator_en: regulator enable flag
 *
 * This structure gives platform data for rmi4.
 */
struct synaptics_rmi4_platform_data {
	int irq_number;
	int irq_type;
	bool virtual_keys;   //virtual_keys for touch screen without real keys
 	int lcd_width;
	int lcd_height;
	int w_delta;
	int h_delta;
	bool x_flip;
	bool y_flip;
	bool regulator_en;
};
#endif

#endif /* _SYNAPTICS_RMI4_H_INCLUDED_ */
