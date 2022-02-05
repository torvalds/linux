/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  Copyright 2017-2021 Broadcom Inc. All rights reserved.
 *
 */
#ifndef MPI30_CNFG_H
#define MPI30_CNFG_H     1
#define MPI3_CONFIG_PAGETYPE_IO_UNIT                    (0x00)
#define MPI3_CONFIG_PAGETYPE_MANUFACTURING              (0x01)
#define MPI3_CONFIG_PAGETYPE_IOC                        (0x02)
#define MPI3_CONFIG_PAGETYPE_DRIVER                     (0x03)
#define MPI3_CONFIG_PAGETYPE_SECURITY                   (0x04)
#define MPI3_CONFIG_PAGETYPE_ENCLOSURE                  (0x11)
#define MPI3_CONFIG_PAGETYPE_DEVICE                     (0x12)
#define MPI3_CONFIG_PAGETYPE_SAS_IO_UNIT                (0x20)
#define MPI3_CONFIG_PAGETYPE_SAS_EXPANDER               (0x21)
#define MPI3_CONFIG_PAGETYPE_SAS_PHY                    (0x23)
#define MPI3_CONFIG_PAGETYPE_SAS_PORT                   (0x24)
#define MPI3_CONFIG_PAGETYPE_PCIE_IO_UNIT               (0x30)
#define MPI3_CONFIG_PAGETYPE_PCIE_SWITCH                (0x31)
#define MPI3_CONFIG_PAGETYPE_PCIE_LINK                  (0x33)
#define MPI3_CONFIG_PAGEATTR_MASK                       (0xf0)
#define MPI3_CONFIG_PAGEATTR_READ_ONLY                  (0x00)
#define MPI3_CONFIG_PAGEATTR_CHANGEABLE                 (0x10)
#define MPI3_CONFIG_PAGEATTR_PERSISTENT                 (0x20)
#define MPI3_CONFIG_ACTION_PAGE_HEADER                  (0x00)
#define MPI3_CONFIG_ACTION_READ_DEFAULT                 (0x01)
#define MPI3_CONFIG_ACTION_READ_CURRENT                 (0x02)
#define MPI3_CONFIG_ACTION_WRITE_CURRENT                (0x03)
#define MPI3_CONFIG_ACTION_READ_PERSISTENT              (0x04)
#define MPI3_CONFIG_ACTION_WRITE_PERSISTENT             (0x05)
#define MPI3_DEVICE_PGAD_FORM_MASK                      (0xf0000000)
#define MPI3_DEVICE_PGAD_FORM_GET_NEXT_HANDLE           (0x00000000)
#define MPI3_DEVICE_PGAD_FORM_HANDLE                    (0x20000000)
#define MPI3_DEVICE_PGAD_HANDLE_MASK                    (0x0000ffff)
#define MPI3_SAS_EXPAND_PGAD_FORM_MASK                  (0xf0000000)
#define MPI3_SAS_EXPAND_PGAD_FORM_GET_NEXT_HANDLE       (0x00000000)
#define MPI3_SAS_EXPAND_PGAD_FORM_HANDLE_PHY_NUM        (0x10000000)
#define MPI3_SAS_EXPAND_PGAD_FORM_HANDLE                (0x20000000)
#define MPI3_SAS_EXPAND_PGAD_PHYNUM_MASK                (0x00ff0000)
#define MPI3_SAS_EXPAND_PGAD_PHYNUM_SHIFT               (16)
#define MPI3_SAS_EXPAND_PGAD_HANDLE_MASK                (0x0000ffff)
#define MPI3_SAS_PHY_PGAD_FORM_MASK                     (0xf0000000)
#define MPI3_SAS_PHY_PGAD_FORM_PHY_NUMBER               (0x00000000)
#define MPI3_SAS_PHY_PGAD_PHY_NUMBER_MASK               (0x000000ff)
#define MPI3_SASPORT_PGAD_FORM_MASK                     (0xf0000000)
#define MPI3_SASPORT_PGAD_FORM_GET_NEXT_PORT            (0x00000000)
#define MPI3_SASPORT_PGAD_FORM_PORT_NUM                 (0x10000000)
#define MPI3_SASPORT_PGAD_PORT_NUMBER_MASK              (0x000000ff)
#define MPI3_ENCLOS_PGAD_FORM_MASK                      (0xf0000000)
#define MPI3_ENCLOS_PGAD_FORM_GET_NEXT_HANDLE           (0x00000000)
#define MPI3_ENCLOS_PGAD_FORM_HANDLE                    (0x10000000)
#define MPI3_ENCLOS_PGAD_HANDLE_MASK                    (0x0000ffff)
#define MPI3_PCIE_SWITCH_PGAD_FORM_MASK                 (0xf0000000)
#define MPI3_PCIE_SWITCH_PGAD_FORM_GET_NEXT_HANDLE      (0x00000000)
#define MPI3_PCIE_SWITCH_PGAD_FORM_HANDLE_PORT_NUM      (0x10000000)
#define MPI3_PCIE_SWITCH_PGAD_FORM_HANDLE               (0x20000000)
#define MPI3_PCIE_SWITCH_PGAD_PORTNUM_MASK              (0x00ff0000)
#define MPI3_PCIE_SWITCH_PGAD_PORTNUM_SHIFT             (16)
#define MPI3_PCIE_SWITCH_PGAD_HANDLE_MASK               (0x0000ffff)
#define MPI3_PCIE_LINK_PGAD_FORM_MASK                   (0xf0000000)
#define MPI3_PCIE_LINK_PGAD_FORM_GET_NEXT_LINK          (0x00000000)
#define MPI3_PCIE_LINK_PGAD_FORM_LINK_NUM               (0x10000000)
#define MPI3_PCIE_LINK_PGAD_LINKNUM_MASK                (0x000000ff)
#define MPI3_SECURITY_PGAD_FORM_MASK                    (0xf0000000)
#define MPI3_SECURITY_PGAD_FORM_GET_NEXT_SLOT           (0x00000000)
#define MPI3_SECURITY_PGAD_FORM_SOT_NUM                 (0x10000000)
#define MPI3_SECURITY_PGAD_SLOT_GROUP_MASK              (0x0000ff00)
#define MPI3_SECURITY_PGAD_SLOT_MASK                    (0x000000ff)
struct mpi3_config_request {
	__le16             host_tag;
	u8                 ioc_use_only02;
	u8                 function;
	__le16             ioc_use_only04;
	u8                 ioc_use_only06;
	u8                 msg_flags;
	__le16             change_count;
	__le16             reserved0a;
	u8                 page_version;
	u8                 page_number;
	u8                 page_type;
	u8                 action;
	__le32             page_address;
	__le16             page_length;
	__le16             reserved16;
	__le32             reserved18[2];
	union mpi3_sge_union  sgl;
};

struct mpi3_config_page_header {
	u8                 page_version;
	u8                 reserved01;
	u8                 page_number;
	u8                 page_attribute;
	__le16             page_length;
	u8                 page_type;
	u8                 reserved07;
};

#define MPI3_SAS_NEG_LINK_RATE_LOGICAL_MASK             (0xf0)
#define MPI3_SAS_NEG_LINK_RATE_LOGICAL_SHIFT            (4)
#define MPI3_SAS_NEG_LINK_RATE_PHYSICAL_MASK            (0x0f)
#define MPI3_SAS_NEG_LINK_RATE_UNKNOWN_LINK_RATE        (0x00)
#define MPI3_SAS_NEG_LINK_RATE_PHY_DISABLED             (0x01)
#define MPI3_SAS_NEG_LINK_RATE_NEGOTIATION_FAILED       (0x02)
#define MPI3_SAS_NEG_LINK_RATE_SATA_OOB_COMPLETE        (0x03)
#define MPI3_SAS_NEG_LINK_RATE_PORT_SELECTOR            (0x04)
#define MPI3_SAS_NEG_LINK_RATE_SMP_RESET_IN_PROGRESS    (0x05)
#define MPI3_SAS_NEG_LINK_RATE_UNSUPPORTED_PHY          (0x06)
#define MPI3_SAS_NEG_LINK_RATE_1_5                      (0x08)
#define MPI3_SAS_NEG_LINK_RATE_3_0                      (0x09)
#define MPI3_SAS_NEG_LINK_RATE_6_0                      (0x0a)
#define MPI3_SAS_NEG_LINK_RATE_12_0                     (0x0b)
#define MPI3_SAS_NEG_LINK_RATE_22_5                     (0x0c)
#define MPI3_SAS_APHYINFO_INSIDE_ZPSDS_PERSISTENT       (0x00000040)
#define MPI3_SAS_APHYINFO_REQUESTED_INSIDE_ZPSDS        (0x00000020)
#define MPI3_SAS_APHYINFO_BREAK_REPLY_CAPABLE           (0x00000010)
#define MPI3_SAS_APHYINFO_REASON_MASK                   (0x0000000f)
#define MPI3_SAS_APHYINFO_REASON_UNKNOWN                (0x00000000)
#define MPI3_SAS_APHYINFO_REASON_POWER_ON               (0x00000001)
#define MPI3_SAS_APHYINFO_REASON_HARD_RESET             (0x00000002)
#define MPI3_SAS_APHYINFO_REASON_SMP_PHY_CONTROL        (0x00000003)
#define MPI3_SAS_APHYINFO_REASON_LOSS_OF_SYNC           (0x00000004)
#define MPI3_SAS_APHYINFO_REASON_MULTIPLEXING_SEQ       (0x00000005)
#define MPI3_SAS_APHYINFO_REASON_IT_NEXUS_LOSS_TIMER    (0x00000006)
#define MPI3_SAS_APHYINFO_REASON_BREAK_TIMEOUT          (0x00000007)
#define MPI3_SAS_APHYINFO_REASON_PHY_TEST_STOPPED       (0x00000008)
#define MPI3_SAS_APHYINFO_REASON_EXP_REDUCED_FUNC       (0x00000009)
#define MPI3_SAS_PHYINFO_STATUS_MASK                    (0xc0000000)
#define MPI3_SAS_PHYINFO_STATUS_SHIFT                   (30)
#define MPI3_SAS_PHYINFO_STATUS_ACCESSIBLE              (0x00000000)
#define MPI3_SAS_PHYINFO_STATUS_NOT_EXIST               (0x40000000)
#define MPI3_SAS_PHYINFO_STATUS_VACANT                  (0x80000000)
#define MPI3_SAS_PHYINFO_PHY_POWER_CONDITION_MASK       (0x18000000)
#define MPI3_SAS_PHYINFO_PHY_POWER_CONDITION_ACTIVE     (0x00000000)
#define MPI3_SAS_PHYINFO_PHY_POWER_CONDITION_PARTIAL    (0x08000000)
#define MPI3_SAS_PHYINFO_PHY_POWER_CONDITION_SLUMBER    (0x10000000)
#define MPI3_SAS_PHYINFO_REASON_MASK                    (0x000f0000)
#define MPI3_SAS_PHYINFO_REASON_UNKNOWN                 (0x00000000)
#define MPI3_SAS_PHYINFO_REASON_POWER_ON                (0x00010000)
#define MPI3_SAS_PHYINFO_REASON_HARD_RESET              (0x00020000)
#define MPI3_SAS_PHYINFO_REASON_SMP_PHY_CONTROL         (0x00030000)
#define MPI3_SAS_PHYINFO_REASON_LOSS_OF_SYNC            (0x00040000)
#define MPI3_SAS_PHYINFO_REASON_MULTIPLEXING_SEQ        (0x00050000)
#define MPI3_SAS_PHYINFO_REASON_IT_NEXUS_LOSS_TIMER     (0x00060000)
#define MPI3_SAS_PHYINFO_REASON_BREAK_TIMEOUT           (0x00070000)
#define MPI3_SAS_PHYINFO_REASON_PHY_TEST_STOPPED        (0x00080000)
#define MPI3_SAS_PHYINFO_REASON_EXP_REDUCED_FUNC        (0x00090000)
#define MPI3_SAS_PHYINFO_SATA_PORT_ACTIVE               (0x00004000)
#define MPI3_SAS_PHYINFO_SATA_PORT_SELECTOR_PRESENT     (0x00002000)
#define MPI3_SAS_PHYINFO_VIRTUAL_PHY                    (0x00001000)
#define MPI3_SAS_PHYINFO_PARTIAL_PATHWAY_TIME_MASK      (0x00000f00)
#define MPI3_SAS_PHYINFO_PARTIAL_PATHWAY_TIME_SHIFT     (8)
#define MPI3_SAS_PHYINFO_ROUTING_ATTRIBUTE_MASK         (0x000000f0)
#define MPI3_SAS_PHYINFO_ROUTING_ATTRIBUTE_DIRECT       (0x00000000)
#define MPI3_SAS_PHYINFO_ROUTING_ATTRIBUTE_SUBTRACTIVE  (0x00000010)
#define MPI3_SAS_PHYINFO_ROUTING_ATTRIBUTE_TABLE        (0x00000020)
#define MPI3_SAS_PRATE_MAX_RATE_MASK                    (0xf0)
#define MPI3_SAS_PRATE_MAX_RATE_NOT_PROGRAMMABLE        (0x00)
#define MPI3_SAS_PRATE_MAX_RATE_1_5                     (0x80)
#define MPI3_SAS_PRATE_MAX_RATE_3_0                     (0x90)
#define MPI3_SAS_PRATE_MAX_RATE_6_0                     (0xa0)
#define MPI3_SAS_PRATE_MAX_RATE_12_0                    (0xb0)
#define MPI3_SAS_PRATE_MAX_RATE_22_5                    (0xc0)
#define MPI3_SAS_PRATE_MIN_RATE_MASK                    (0x0f)
#define MPI3_SAS_PRATE_MIN_RATE_NOT_PROGRAMMABLE        (0x00)
#define MPI3_SAS_PRATE_MIN_RATE_1_5                     (0x08)
#define MPI3_SAS_PRATE_MIN_RATE_3_0                     (0x09)
#define MPI3_SAS_PRATE_MIN_RATE_6_0                     (0x0a)
#define MPI3_SAS_PRATE_MIN_RATE_12_0                    (0x0b)
#define MPI3_SAS_PRATE_MIN_RATE_22_5                    (0x0c)
#define MPI3_SAS_HWRATE_MAX_RATE_MASK                   (0xf0)
#define MPI3_SAS_HWRATE_MAX_RATE_1_5                    (0x80)
#define MPI3_SAS_HWRATE_MAX_RATE_3_0                    (0x90)
#define MPI3_SAS_HWRATE_MAX_RATE_6_0                    (0xa0)
#define MPI3_SAS_HWRATE_MAX_RATE_12_0                   (0xb0)
#define MPI3_SAS_HWRATE_MAX_RATE_22_5                   (0xc0)
#define MPI3_SAS_HWRATE_MIN_RATE_MASK                   (0x0f)
#define MPI3_SAS_HWRATE_MIN_RATE_1_5                    (0x08)
#define MPI3_SAS_HWRATE_MIN_RATE_3_0                    (0x09)
#define MPI3_SAS_HWRATE_MIN_RATE_6_0                    (0x0a)
#define MPI3_SAS_HWRATE_MIN_RATE_12_0                   (0x0b)
#define MPI3_SAS_HWRATE_MIN_RATE_22_5                   (0x0c)
#define MPI3_SLOT_INVALID                               (0xffff)
#define MPI3_SLOT_INDEX_INVALID                         (0xffff)
#define MPI3_LINK_CHANGE_COUNT_INVALID                   (0xffff)
#define MPI3_RATE_CHANGE_COUNT_INVALID                   (0xffff)
#define MPI3_TEMP_SENSOR_LOCATION_INTERNAL              (0x0)
#define MPI3_TEMP_SENSOR_LOCATION_INLET                 (0x1)
#define MPI3_TEMP_SENSOR_LOCATION_OUTLET                (0x2)
#define MPI3_TEMP_SENSOR_LOCATION_DRAM                  (0x3)
#define MPI3_MFGPAGE_VENDORID_BROADCOM                  (0x1000)
#define MPI3_MFGPAGE_DEVID_SAS4116                      (0x00a5)
#define MPI3_MFGPAGE_DEVID_SAS4016                      (0x00a7)
struct mpi3_man_page0 {
	struct mpi3_config_page_header         header;
	u8                                 chip_revision[8];
	u8                                 chip_name[32];
	u8                                 board_name[32];
	u8                                 board_assembly[32];
	u8                                 board_tracer_number[32];
	__le32                             board_power;
	__le32                             reserved94;
	__le32                             reserved98;
	u8                                 oem;
	u8                                 sub_oem;
	__le16                             flags;
	u8                                 board_mfg_day;
	u8                                 board_mfg_month;
	__le16                             board_mfg_year;
	u8                                 board_rework_day;
	u8                                 board_rework_month;
	__le16                             board_rework_year;
	__le64                             board_revision;
	u8                                 e_pack_fru[16];
	u8                                 product_name[256];
};

#define MPI3_MAN0_PAGEVERSION       (0x00)
#define MPI3_MAN0_FLAGS_SWITCH_PRESENT                       (0x0002)
#define MPI3_MAN0_FLAGS_EXPANDER_PRESENT                     (0x0001)
#define MPI3_MAN1_VPD_SIZE                                   (512)
struct mpi3_man_page1 {
	struct mpi3_config_page_header         header;
	__le32                             reserved08[2];
	u8                                 vpd[MPI3_MAN1_VPD_SIZE];
};

#define MPI3_MAN1_PAGEVERSION                                 (0x00)
struct mpi3_man5_phy_entry {
	__le64     ioc_wwid;
	__le64     device_name;
	__le64     sata_wwid;
};

#ifndef MPI3_MAN5_PHY_MAX
#define MPI3_MAN5_PHY_MAX                                   (1)
#endif
struct mpi3_man_page5 {
	struct mpi3_config_page_header         header;
	u8                                 num_phys;
	u8                                 reserved09[3];
	__le32                             reserved0c;
	struct mpi3_man5_phy_entry             phy[MPI3_MAN5_PHY_MAX];
};

#define MPI3_MAN5_PAGEVERSION                                (0x00)
struct mpi3_man6_gpio_entry {
	u8         function_code;
	u8         function_flags;
	__le16     flags;
	u8         param1;
	u8         param2;
	__le16     reserved06;
	__le32     param3;
};

#define MPI3_MAN6_GPIO_FUNCTION_GENERIC                                       (0x00)
#define MPI3_MAN6_GPIO_FUNCTION_ALTERNATE                                     (0x01)
#define MPI3_MAN6_GPIO_FUNCTION_EXT_INTERRUPT                                 (0x02)
#define MPI3_MAN6_GPIO_FUNCTION_GLOBAL_ACTIVITY                               (0x03)
#define MPI3_MAN6_GPIO_FUNCTION_OVER_TEMPERATURE                              (0x04)
#define MPI3_MAN6_GPIO_FUNCTION_PORT_STATUS_GREEN                             (0x05)
#define MPI3_MAN6_GPIO_FUNCTION_PORT_STATUS_YELLOW                            (0x06)
#define MPI3_MAN6_GPIO_FUNCTION_CABLE_MANAGEMENT                              (0x07)
#define MPI3_MAN6_GPIO_FUNCTION_BKPLANE_MGMT_TYPE                             (0x08)
#define MPI3_MAN6_GPIO_FUNCTION_ISTWI_RESET                                   (0x0a)
#define MPI3_MAN6_GPIO_FUNCTION_BACKEND_PCIE_RESET                            (0x0b)
#define MPI3_MAN6_GPIO_FUNCTION_GLOBAL_FAULT                                  (0x0c)
#define MPI3_MAN6_GPIO_FUNCTION_EPACK_ATTN                                    (0x0d)
#define MPI3_MAN6_GPIO_FUNCTION_EPACK_ONLINE                                  (0x0e)
#define MPI3_MAN6_GPIO_FUNCTION_EPACK_FAULT                                   (0x0f)
#define MPI3_MAN6_GPIO_FUNCTION_CTRL_TYPE                                     (0x10)
#define MPI3_MAN6_GPIO_FUNCTION_LICENSE                                       (0x11)
#define MPI3_MAN6_GPIO_FUNCTION_REFCLK_CONTROL                                (0x12)
#define MPI3_MAN6_GPIO_FUNCTION_BACKEND_PCIE_RESET_CLAMP                      (0x13)
#define MPI3_MAN6_GPIO_ISTWI_RESET_FUNCTIONFLAGS_DEVSELECT_MASK               (0x01)
#define MPI3_MAN6_GPIO_ISTWI_RESET_FUNCTIONFLAGS_DEVSELECT_ISTWI              (0x00)
#define MPI3_MAN6_GPIO_ISTWI_RESET_FUNCTIONFLAGS_DEVSELECT_RECEPTACLEID       (0x01)
#define MPI3_MAN6_GPIO_EXTINT_PARAM1_FLAGS_SOURCE_MASK                        (0xf0)
#define MPI3_MAN6_GPIO_EXTINT_PARAM1_FLAGS_SOURCE_GENERIC                     (0x00)
#define MPI3_MAN6_GPIO_EXTINT_PARAM1_FLAGS_SOURCE_CABLE_MGMT                  (0x10)
#define MPI3_MAN6_GPIO_EXTINT_PARAM1_FLAGS_SOURCE_ACTIVE_CABLE_OVERCURRENT    (0x20)
#define MPI3_MAN6_GPIO_EXTINT_PARAM1_FLAGS_TRIGGER_MASK                       (0x01)
#define MPI3_MAN6_GPIO_EXTINT_PARAM1_FLAGS_TRIGGER_EDGE                       (0x00)
#define MPI3_MAN6_GPIO_EXTINT_PARAM1_FLAGS_TRIGGER_LEVEL                      (0x01)
#define MPI3_MAN6_GPIO_PORT_GREEN_PARAM1_PHY_STATUS_ALL_UP                    (0x00)
#define MPI3_MAN6_GPIO_PORT_GREEN_PARAM1_PHY_STATUS_ONE_OR_MORE_UP            (0x01)
#define MPI3_MAN6_GPIO_CABLE_MGMT_PARAM1_INTERFACE_MODULE_PRESENT             (0x00)
#define MPI3_MAN6_GPIO_CABLE_MGMT_PARAM1_INTERFACE_ACTIVE_CABLE_ENABLE        (0x01)
#define MPI3_MAN6_GPIO_CABLE_MGMT_PARAM1_INTERFACE_CABLE_MGMT_ENABLE          (0x02)
#define MPI3_MAN6_GPIO_LICENSE_PARAM1_TYPE_IBUTTON                            (0x00)
#define MPI3_MAN6_GPIO_FLAGS_SLEW_RATE_MASK                                   (0x0100)
#define MPI3_MAN6_GPIO_FLAGS_SLEW_RATE_FAST_EDGE                              (0x0100)
#define MPI3_MAN6_GPIO_FLAGS_SLEW_RATE_SLOW_EDGE                              (0x0000)
#define MPI3_MAN6_GPIO_FLAGS_DRIVE_STRENGTH_MASK                              (0x00c0)
#define MPI3_MAN6_GPIO_FLAGS_DRIVE_STRENGTH_100OHM                            (0x0000)
#define MPI3_MAN6_GPIO_FLAGS_DRIVE_STRENGTH_66OHM                             (0x0040)
#define MPI3_MAN6_GPIO_FLAGS_DRIVE_STRENGTH_50OHM                             (0x0080)
#define MPI3_MAN6_GPIO_FLAGS_DRIVE_STRENGTH_33OHM                             (0x00c0)
#define MPI3_MAN6_GPIO_FLAGS_ALT_DATA_SEL_MASK                                (0x0030)
#define MPI3_MAN6_GPIO_FLAGS_ALT_DATA_SEL_SHIFT                               (4)
#define MPI3_MAN6_GPIO_FLAGS_ACTIVE_HIGH                                      (0x0008)
#define MPI3_MAN6_GPIO_FLAGS_BI_DIR_ENABLED                                   (0x0004)
#define MPI3_MAN6_GPIO_FLAGS_DIRECTION_MASK                                   (0x0003)
#define MPI3_MAN6_GPIO_FLAGS_DIRECTION_INPUT                                  (0x0000)
#define MPI3_MAN6_GPIO_FLAGS_DIRECTION_OPEN_DRAIN_OUTPUT                      (0x0001)
#define MPI3_MAN6_GPIO_FLAGS_DIRECTION_OPEN_SOURCE_OUTPUT                     (0x0002)
#define MPI3_MAN6_GPIO_FLAGS_DIRECTION_PUSH_PULL_OUTPUT                       (0x0003)
#ifndef MPI3_MAN6_GPIO_MAX
#define MPI3_MAN6_GPIO_MAX                                                    (1)
#endif
struct mpi3_man_page6 {
	struct mpi3_config_page_header         header;
	__le16                             flags;
	__le16                             reserved0a;
	u8                                 num_gpio;
	u8                                 reserved0d[3];
	struct mpi3_man6_gpio_entry            gpio[MPI3_MAN6_GPIO_MAX];
};

#define MPI3_MAN6_PAGEVERSION                                                 (0x00)
#define MPI3_MAN6_FLAGS_HEARTBEAT_LED_DISABLED                                (0x0001)
struct mpi3_man7_receptacle_info {
	__le32                             name[4];
	u8                                 location;
	u8                                 connector_type;
	u8                                 ped_clk;
	u8                                 connector_id;
	__le32                             reserved14;
};

#define MPI3_MAN7_LOCATION_UNKNOWN                         (0x00)
#define MPI3_MAN7_LOCATION_INTERNAL                        (0x01)
#define MPI3_MAN7_LOCATION_EXTERNAL                        (0x02)
#define MPI3_MAN7_LOCATION_VIRTUAL                         (0x03)
#define MPI3_MAN7_PEDCLK_ROUTING_MASK                      (0x10)
#define MPI3_MAN7_PEDCLK_ROUTING_DIRECT                    (0x00)
#define MPI3_MAN7_PEDCLK_ROUTING_CLOCK_BUFFER              (0x10)
#define MPI3_MAN7_PEDCLK_ID_MASK                           (0x0f)
#ifndef MPI3_MAN7_RECEPTACLE_INFO_MAX
#define MPI3_MAN7_RECEPTACLE_INFO_MAX                      (1)
#endif
struct mpi3_man_page7 {
	struct mpi3_config_page_header         header;
	__le32                             flags;
	u8                                 num_receptacles;
	u8                                 reserved0d[3];
	__le32                             enclosure_name[4];
	struct mpi3_man7_receptacle_info       receptacle_info[MPI3_MAN7_RECEPTACLE_INFO_MAX];
};

#define MPI3_MAN7_PAGEVERSION                              (0x00)
#define MPI3_MAN7_FLAGS_BASE_ENCLOSURE_LEVEL_MASK          (0x01)
#define MPI3_MAN7_FLAGS_BASE_ENCLOSURE_LEVEL_0             (0x00)
#define MPI3_MAN7_FLAGS_BASE_ENCLOSURE_LEVEL_1             (0x01)
struct mpi3_man8_phy_info {
	u8                                 receptacle_id;
	u8                                 connector_lane;
	__le16                             reserved02;
	__le16                             slotx1;
	__le16                             slotx2;
	__le16                             slotx4;
	__le16                             reserved0a;
	__le32                             reserved0c;
};

#define MPI3_MAN8_PHY_INFO_RECEPTACLE_ID_HOST_PHY          (0xff)
#ifndef MPI3_MAN8_PHY_INFO_MAX
#define MPI3_MAN8_PHY_INFO_MAX                      (1)
#endif
struct mpi3_man_page8 {
	struct mpi3_config_page_header         header;
	__le32                             reserved08;
	u8                                 num_phys;
	u8                                 reserved0d[3];
	struct mpi3_man8_phy_info              phy_info[MPI3_MAN8_PHY_INFO_MAX];
};

#define MPI3_MAN8_PAGEVERSION                   (0x00)
struct mpi3_man9_rsrc_entry {
	__le32     maximum;
	__le32     decrement;
	__le32     minimum;
	__le32     actual;
};

enum mpi3_man9_resources {
	MPI3_MAN9_RSRC_OUTSTANDING_REQS    = 0,
	MPI3_MAN9_RSRC_TARGET_CMDS         = 1,
	MPI3_MAN9_RSRC_RESERVED02          = 2,
	MPI3_MAN9_RSRC_NVME                = 3,
	MPI3_MAN9_RSRC_INITIATORS          = 4,
	MPI3_MAN9_RSRC_VDS                 = 5,
	MPI3_MAN9_RSRC_ENCLOSURES          = 6,
	MPI3_MAN9_RSRC_ENCLOSURE_PHYS      = 7,
	MPI3_MAN9_RSRC_EXPANDERS           = 8,
	MPI3_MAN9_RSRC_PCIE_SWITCHES       = 9,
	MPI3_MAN9_RSRC_RESERVED10          = 10,
	MPI3_MAN9_RSRC_HOST_PD_DRIVES      = 11,
	MPI3_MAN9_RSRC_ADV_HOST_PD_DRIVES  = 12,
	MPI3_MAN9_RSRC_RAID_PD_DRIVES      = 13,
	MPI3_MAN9_RSRC_DRV_DIAG_BUF        = 14,
	MPI3_MAN9_RSRC_NAMESPACE_COUNT     = 15,
	MPI3_MAN9_RSRC_NUM_RESOURCES
};

#define MPI3_MAN9_MIN_OUTSTANDING_REQS      (1)
#define MPI3_MAN9_MAX_OUTSTANDING_REQS      (65000)
#define MPI3_MAN9_MIN_TARGET_CMDS           (0)
#define MPI3_MAN9_MAX_TARGET_CMDS           (65535)
#define MPI3_MAN9_MIN_SAS_TARGETS           (0)
#define MPI3_MAN9_MAX_SAS_TARGETS           (65535)
#define MPI3_MAN9_MIN_PCIE_TARGETS          (0)
#define MPI3_MAN9_MIN_INITIATORS            (0)
#define MPI3_MAN9_MAX_INITIATORS            (65535)
#define MPI3_MAN9_MIN_ENCLOSURES            (0)
#define MPI3_MAN9_MAX_ENCLOSURES            (65535)
#define MPI3_MAN9_MIN_ENCLOSURE_PHYS        (0)
#define MPI3_MAN9_MIN_NAMESPACE_COUNT       (1)
#define MPI3_MAN9_MIN_EXPANDERS             (0)
#define MPI3_MAN9_MAX_EXPANDERS             (65535)
#define MPI3_MAN9_MIN_PCIE_SWITCHES         (0)
struct mpi3_man_page9 {
	struct mpi3_config_page_header         header;
	u8                                 num_resources;
	u8                                 reserved09;
	__le16                             reserved0a;
	__le32                             reserved0c;
	__le32                             reserved10;
	__le32                             reserved14;
	__le32                             reserved18;
	__le32                             reserved1c;
	struct mpi3_man9_rsrc_entry            resource[MPI3_MAN9_RSRC_NUM_RESOURCES];
};

#define MPI3_MAN9_PAGEVERSION                   (0x00)
struct mpi3_man10_istwi_ctrlr_entry {
	__le16     slave_address;
	__le16     flags;
	u8         scl_low_override;
	u8         scl_high_override;
	__le16     reserved06;
};

#define MPI3_MAN10_ISTWI_CTRLR_FLAGS_BUS_SPEED_MASK         (0x000c)
#define MPI3_MAN10_ISTWI_CTRLR_FLAGS_BUS_SPEED_100K         (0x0000)
#define MPI3_MAN10_ISTWI_CTRLR_FLAGS_BUS_SPEED_400K         (0x0004)
#define MPI3_MAN10_ISTWI_CTRLR_FLAGS_SLAVE_ENABLED          (0x0002)
#define MPI3_MAN10_ISTWI_CTRLR_FLAGS_MASTER_ENABLED         (0x0001)
#ifndef MPI3_MAN10_ISTWI_CTRLR_MAX
#define MPI3_MAN10_ISTWI_CTRLR_MAX          (1)
#endif
struct mpi3_man_page10 {
	struct mpi3_config_page_header         header;
	__le32                             reserved08;
	u8                                 num_istwi_ctrl;
	u8                                 reserved0d[3];
	struct mpi3_man10_istwi_ctrlr_entry    istwi_controller[MPI3_MAN10_ISTWI_CTRLR_MAX];
};

#define MPI3_MAN10_PAGEVERSION                  (0x00)
struct mpi3_man11_mux_device_format {
	u8         max_channel;
	u8         reserved01[3];
	__le32     reserved04;
};

struct mpi3_man11_temp_sensor_device_format {
	u8         type;
	u8         reserved01[3];
	u8         temp_channel[4];
};

#define MPI3_MAN11_TEMP_SENSOR_TYPE_MAX6654                (0x00)
#define MPI3_MAN11_TEMP_SENSOR_TYPE_EMC1442                (0x01)
#define MPI3_MAN11_TEMP_SENSOR_TYPE_ADT7476                (0x02)
#define MPI3_MAN11_TEMP_SENSOR_TYPE_SE97B                  (0x03)
#define MPI3_MAN11_TEMP_SENSOR_CHANNEL_LOCATION_MASK       (0xe0)
#define MPI3_MAN11_TEMP_SENSOR_CHANNEL_LOCATION_SHIFT      (5)
#define MPI3_MAN11_TEMP_SENSOR_CHANNEL_ENABLED             (0x01)
struct mpi3_man11_seeprom_device_format {
	u8         size;
	u8         page_write_size;
	__le16     reserved02;
	__le32     reserved04;
};

#define MPI3_MAN11_SEEPROM_SIZE_1KBITS              (0x01)
#define MPI3_MAN11_SEEPROM_SIZE_2KBITS              (0x02)
#define MPI3_MAN11_SEEPROM_SIZE_4KBITS              (0x03)
#define MPI3_MAN11_SEEPROM_SIZE_8KBITS              (0x04)
#define MPI3_MAN11_SEEPROM_SIZE_16KBITS             (0x05)
#define MPI3_MAN11_SEEPROM_SIZE_32KBITS             (0x06)
#define MPI3_MAN11_SEEPROM_SIZE_64KBITS             (0x07)
#define MPI3_MAN11_SEEPROM_SIZE_128KBITS            (0x08)
struct mpi3_man11_ddr_spd_device_format {
	u8         channel;
	u8         reserved01[3];
	__le32     reserved04;
};

struct mpi3_man11_cable_mgmt_device_format {
	u8         type;
	u8         receptacle_id;
	__le16     reserved02;
	__le32     reserved04;
};

#define MPI3_MAN11_CABLE_MGMT_TYPE_SFF_8636           (0x00)
struct mpi3_man11_bkplane_spec_ubm_format {
	__le16     flags;
	__le16     reserved02;
};

#define MPI3_MAN11_BKPLANE_UBM_FLAGS_REFCLK_POLICY_ALWAYS_ENABLED  (0x0200)
#define MPI3_MAN11_BKPLANE_UBM_FLAGS_FORCE_POLLING                 (0x0100)
#define MPI3_MAN11_BKPLANE_UBM_FLAGS_MAX_FRU_MASK                  (0x00f0)
#define MPI3_MAN11_BKPLANE_UBM_FLAGS_MAX_FRU_SHIFT                 (4)
#define MPI3_MAN11_BKPLANE_UBM_FLAGS_POLL_INTERVAL_MASK            (0x000f)
#define MPI3_MAN11_BKPLANE_UBM_FLAGS_POLL_INTERVAL_SHIFT           (0)
struct mpi3_man11_bkplane_spec_non_ubm_format {
	__le16     flags;
	u8         reserved02;
	u8         type;
};

#define MPI3_MAN11_BKPLANE_NON_UBM_FLAGS_GROUP_MASK                    (0xf000)
#define MPI3_MAN11_BKPLANE_NON_UBM_FLAGS_GROUP_SHIFT                   (12)
#define MPI3_MAN11_BKPLANE_NON_UBM_FLAGS_REFCLK_POLICY_ALWAYS_ENABLED  (0x0200)
#define MPI3_MAN11_BKPLANE_NON_UBM_FLAGS_PRESENCE_DETECT_MASK          (0x0030)
#define MPI3_MAN11_BKPLANE_NON_UBM_FLAGS_PRESENCE_DETECT_GPIO          (0x0000)
#define MPI3_MAN11_BKPLANE_NON_UBM_FLAGS_PRESENCE_DETECT_REG           (0x0010)
#define MPI3_MAN11_BKPLANE_NON_UBM_FLAGS_POLL_INTERVAL_MASK            (0x000f)
#define MPI3_MAN11_BKPLANE_NON_UBM_FLAGS_POLL_INTERVAL_SHIFT           (0)
#define MPI3_MAN11_BKPLANE_NON_UBM_TYPE_VPP                            (0x00)
union mpi3_man11_bkplane_spec_format {
	struct mpi3_man11_bkplane_spec_ubm_format         ubm;
	struct mpi3_man11_bkplane_spec_non_ubm_format     non_ubm;
};

struct mpi3_man11_bkplane_mgmt_device_format {
	u8                                        type;
	u8                                        receptacle_id;
	u8                                        reset_info;
	u8                                        reserved03;
	union mpi3_man11_bkplane_spec_format         backplane_mgmt_specific;
};

#define MPI3_MAN11_BKPLANE_MGMT_TYPE_UBM            (0x00)
#define MPI3_MAN11_BKPLANE_MGMT_TYPE_NON_UBM        (0x01)
#define MPI3_MAN11_BACKPLANE_RESETINFO_ASSERT_TIME_MASK       (0xf0)
#define MPI3_MAN11_BACKPLANE_RESETINFO_ASSERT_TIME_SHIFT      (4)
#define MPI3_MAN11_BACKPLANE_RESETINFO_READY_TIME_MASK        (0x0f)
#define MPI3_MAN11_BACKPLANE_RESETINFO_READY_TIME_SHIFT       (0)
struct mpi3_man11_gas_gauge_device_format {
	u8         type;
	u8         reserved01[3];
	__le32     reserved04;
};

#define MPI3_MAN11_GAS_GAUGE_TYPE_STANDARD          (0x00)
struct mpi3_man11_mgmt_ctrlr_device_format {
	__le32     reserved00;
	__le32     reserved04;
};

union mpi3_man11_device_specific_format {
	struct mpi3_man11_mux_device_format            mux;
	struct mpi3_man11_temp_sensor_device_format    temp_sensor;
	struct mpi3_man11_seeprom_device_format        seeprom;
	struct mpi3_man11_ddr_spd_device_format        ddr_spd;
	struct mpi3_man11_cable_mgmt_device_format     cable_mgmt;
	struct mpi3_man11_bkplane_mgmt_device_format   bkplane_mgmt;
	struct mpi3_man11_gas_gauge_device_format      gas_gauge;
	struct mpi3_man11_mgmt_ctrlr_device_format     mgmt_controller;
	__le32                                     words[2];
};

struct mpi3_man11_istwi_device_format {
	u8                                     device_type;
	u8                                     controller;
	u8                                     reserved02;
	u8                                     flags;
	__le16                                 device_address;
	u8                                     mux_channel;
	u8                                     mux_index;
	union mpi3_man11_device_specific_format   device_specific;
};

#define MPI3_MAN11_ISTWI_DEVTYPE_MUX                  (0x00)
#define MPI3_MAN11_ISTWI_DEVTYPE_TEMP_SENSOR          (0x01)
#define MPI3_MAN11_ISTWI_DEVTYPE_SEEPROM              (0x02)
#define MPI3_MAN11_ISTWI_DEVTYPE_DDR_SPD              (0x03)
#define MPI3_MAN11_ISTWI_DEVTYPE_CABLE_MGMT           (0x04)
#define MPI3_MAN11_ISTWI_DEVTYPE_BACKPLANE_MGMT       (0x05)
#define MPI3_MAN11_ISTWI_DEVTYPE_GAS_GAUGE            (0x06)
#define MPI3_MAN11_ISTWI_DEVTYPE_MGMT_CONTROLLER      (0x07)
#define MPI3_MAN11_ISTWI_FLAGS_MUX_PRESENT            (0x01)
#ifndef MPI3_MAN11_ISTWI_DEVICE_MAX
#define MPI3_MAN11_ISTWI_DEVICE_MAX             (1)
#endif
struct mpi3_man_page11 {
	struct mpi3_config_page_header         header;
	__le32                             reserved08;
	u8                                 num_istwi_dev;
	u8                                 reserved0d[3];
	struct mpi3_man11_istwi_device_format  istwi_device[MPI3_MAN11_ISTWI_DEVICE_MAX];
};

#define MPI3_MAN11_PAGEVERSION                  (0x00)
#ifndef MPI3_MAN12_NUM_SGPIO_MAX
#define MPI3_MAN12_NUM_SGPIO_MAX                                     (1)
#endif
struct mpi3_man12_sgpio_info {
	u8                                 slot_count;
	u8                                 reserved01[3];
	__le32                             reserved04;
	u8                                 phy_order[32];
};

struct mpi3_man_page12 {
	struct mpi3_config_page_header         header;
	__le32                             flags;
	__le32                             s_clock_freq;
	__le32                             activity_modulation;
	u8                                 num_sgpio;
	u8                                 reserved15[3];
	__le32                             reserved18;
	__le32                             reserved1c;
	__le32                             pattern[8];
	struct mpi3_man12_sgpio_info           sgpio_info[MPI3_MAN12_NUM_SGPIO_MAX];
};

#define MPI3_MAN12_PAGEVERSION                                       (0x00)
#define MPI3_MAN12_FLAGS_ERROR_PRESENCE_ENABLED                      (0x0400)
#define MPI3_MAN12_FLAGS_ACTIVITY_INVERT_ENABLED                     (0x0200)
#define MPI3_MAN12_FLAGS_GROUP_ID_DISABLED                           (0x0100)
#define MPI3_MAN12_FLAGS_SIO_CLK_FILTER_ENABLED                      (0x0004)
#define MPI3_MAN12_FLAGS_SCLOCK_SLOAD_TYPE_MASK                      (0x0002)
#define MPI3_MAN12_FLAGS_SCLOCK_SLOAD_TYPE_PUSH_PULL                 (0x0000)
#define MPI3_MAN12_FLAGS_SCLOCK_SLOAD_TYPE_OPEN_DRAIN                (0x0002)
#define MPI3_MAN12_FLAGS_SDATAOUT_TYPE_MASK                          (0x0001)
#define MPI3_MAN12_FLAGS_SDATAOUT_TYPE_PUSH_PULL                     (0x0000)
#define MPI3_MAN12_FLAGS_SDATAOUT_TYPE_OPEN_DRAIN                    (0x0001)
#define MPI3_MAN12_SIO_CLK_FREQ_MIN                                  (32)
#define MPI3_MAN12_SIO_CLK_FREQ_MAX                                  (100000)
#define MPI3_MAN12_ACTIVITY_MODULATION_FORCE_OFF_MASK                (0x0000f000)
#define MPI3_MAN12_ACTIVITY_MODULATION_FORCE_OFF_SHIFT               (12)
#define MPI3_MAN12_ACTIVITY_MODULATION_MAX_ON_MASK                   (0x00000f00)
#define MPI3_MAN12_ACTIVITY_MODULATION_MAX_ON_SHIFT                  (8)
#define MPI3_MAN12_ACTIVITY_MODULATION_STRETCH_OFF_MASK              (0x000000f0)
#define MPI3_MAN12_ACTIVITY_MODULATION_STRETCH_OFF_SHIFT             (4)
#define MPI3_MAN12_ACTIVITY_MODULATION_STRETCH_ON_MASK               (0x0000000f)
#define MPI3_MAN12_ACTIVITY_MODULATION_STRETCH_ON_SHIFT              (0)
#define MPI3_MAN12_PATTERN_RATE_MASK                                 (0xe0000000)
#define MPI3_MAN12_PATTERN_RATE_2_HZ                                 (0x00000000)
#define MPI3_MAN12_PATTERN_RATE_4_HZ                                 (0x20000000)
#define MPI3_MAN12_PATTERN_RATE_8_HZ                                 (0x40000000)
#define MPI3_MAN12_PATTERN_RATE_16_HZ                                (0x60000000)
#define MPI3_MAN12_PATTERN_RATE_10_HZ                                (0x80000000)
#define MPI3_MAN12_PATTERN_RATE_20_HZ                                (0xa0000000)
#define MPI3_MAN12_PATTERN_RATE_40_HZ                                (0xc0000000)
#define MPI3_MAN12_PATTERN_LENGTH_MASK                               (0x1f000000)
#define MPI3_MAN12_PATTERN_LENGTH_SHIFT                              (24)
#define MPI3_MAN12_PATTERN_BIT_PATTERN_MASK                          (0x00ffffff)
#define MPI3_MAN12_PATTERN_BIT_PATTERN_SHIFT                         (0)
#ifndef MPI3_MAN13_NUM_TRANSLATION_MAX
#define MPI3_MAN13_NUM_TRANSLATION_MAX                               (1)
#endif
struct mpi3_man13_translation_info {
	__le32                             slot_status;
	__le32                             mask;
	u8                                 activity;
	u8                                 locate;
	u8                                 error;
	u8                                 reserved0b;
};

#define MPI3_MAN13_TRANSLATION_SLOTSTATUS_FAULT                     (0x20000000)
#define MPI3_MAN13_TRANSLATION_SLOTSTATUS_DEVICE_OFF                (0x10000000)
#define MPI3_MAN13_TRANSLATION_SLOTSTATUS_DEVICE_ACTIVITY           (0x00800000)
#define MPI3_MAN13_TRANSLATION_SLOTSTATUS_DO_NOT_REMOVE             (0x00400000)
#define MPI3_MAN13_TRANSLATION_SLOTSTATUS_DEVICE_MISSING            (0x00100000)
#define MPI3_MAN13_TRANSLATION_SLOTSTATUS_INSERT                    (0x00080000)
#define MPI3_MAN13_TRANSLATION_SLOTSTATUS_REMOVAL                   (0x00040000)
#define MPI3_MAN13_TRANSLATION_SLOTSTATUS_IDENTIFY                  (0x00020000)
#define MPI3_MAN13_TRANSLATION_SLOTSTATUS_OK                        (0x00008000)
#define MPI3_MAN13_TRANSLATION_SLOTSTATUS_RESERVED_DEVICE           (0x00004000)
#define MPI3_MAN13_TRANSLATION_SLOTSTATUS_HOT_SPARE                 (0x00002000)
#define MPI3_MAN13_TRANSLATION_SLOTSTATUS_CONSISTENCY_CHECK         (0x00001000)
#define MPI3_MAN13_TRANSLATION_SLOTSTATUS_IN_CRITICAL_ARRAY         (0x00000800)
#define MPI3_MAN13_TRANSLATION_SLOTSTATUS_IN_FAILED_ARRAY           (0x00000400)
#define MPI3_MAN13_TRANSLATION_SLOTSTATUS_REBUILD_REMAP             (0x00000200)
#define MPI3_MAN13_TRANSLATION_SLOTSTATUS_REBUILD_REMAP_ABORT       (0x00000100)
#define MPI3_MAN13_TRANSLATION_SLOTSTATUS_PREDICTED_FAILURE         (0x00000040)
#define MPI3_MAN13_BLINK_PATTERN_FORCE_OFF                          (0x00)
#define MPI3_MAN13_BLINK_PATTERN_FORCE_ON                           (0x01)
#define MPI3_MAN13_BLINK_PATTERN_PATTERN_0                          (0x02)
#define MPI3_MAN13_BLINK_PATTERN_PATTERN_1                          (0x03)
#define MPI3_MAN13_BLINK_PATTERN_PATTERN_2                          (0x04)
#define MPI3_MAN13_BLINK_PATTERN_PATTERN_3                          (0x05)
#define MPI3_MAN13_BLINK_PATTERN_PATTERN_4                          (0x06)
#define MPI3_MAN13_BLINK_PATTERN_PATTERN_5                          (0x07)
#define MPI3_MAN13_BLINK_PATTERN_PATTERN_6                          (0x08)
#define MPI3_MAN13_BLINK_PATTERN_PATTERN_7                          (0x09)
#define MPI3_MAN13_BLINK_PATTERN_ACTIVITY                           (0x0a)
#define MPI3_MAN13_BLINK_PATTERN_ACTIVITY_TRAIL                     (0x0b)
struct mpi3_man_page13 {
	struct mpi3_config_page_header         header;
	u8                                 num_trans;
	u8                                 reserved09[3];
	__le32                             reserved0c;
	struct mpi3_man13_translation_info     translation[MPI3_MAN13_NUM_TRANSLATION_MAX];
};

#define MPI3_MAN13_PAGEVERSION                                       (0x00)
struct mpi3_man_page14 {
	struct mpi3_config_page_header         header;
	__le16                             flags;
	__le16                             reserved0a;
	u8                                 num_slot_groups;
	u8                                 num_slots;
	__le16                             max_cert_chain_length;
	__le32                             sealed_slots;
};

#define MPI3_MAN14_PAGEVERSION                                       (0x00)
#define MPI3_MAN14_FLAGS_AUTH_SESSION_REQ                            (0x01)
#define MPI3_MAN14_FLAGS_AUTH_API_MASK                               (0x0e)
#define MPI3_MAN14_FLAGS_AUTH_API_NONE                               (0x00)
#define MPI3_MAN14_FLAGS_AUTH_API_CERBERUS                           (0x02)
#define MPI3_MAN14_FLAGS_AUTH_API_SPDM                               (0x04)
#ifndef MPI3_MAN15_VERSION_RECORD_MAX
#define MPI3_MAN15_VERSION_RECORD_MAX      1
#endif
struct mpi3_man15_version_record {
	__le16                             spdm_version;
	__le16                             reserved02;
};

struct mpi3_man_page15 {
	struct mpi3_config_page_header         header;
	u8                                 num_version_records;
	u8                                 reserved09[3];
	__le32                             reserved0c;
	struct mpi3_man15_version_record       version_record[MPI3_MAN15_VERSION_RECORD_MAX];
};

#define MPI3_MAN15_PAGEVERSION                                       (0x00)
#ifndef MPI3_MAN16_CERT_ALGO_MAX
#define MPI3_MAN16_CERT_ALGO_MAX      1
#endif
struct mpi3_man16_certificate_algorithm {
	u8                                      slot_group;
	u8                                      reserved01[3];
	__le32                                  base_asym_algo;
	__le32                                  base_hash_algo;
	__le32                                  reserved0c[3];
};

struct mpi3_man_page16 {
	struct mpi3_config_page_header              header;
	__le32                                  reserved08;
	u8                                      num_cert_algos;
	u8                                      reserved0d[3];
	struct mpi3_man16_certificate_algorithm     certificate_algorithm[MPI3_MAN16_CERT_ALGO_MAX];
};

#define MPI3_MAN16_PAGEVERSION                                       (0x00)
#ifndef MPI3_MAN17_HASH_ALGORITHM_MAX
#define MPI3_MAN17_HASH_ALGORITHM_MAX      1
#endif
struct mpi3_man17_hash_algorithm {
	u8                                 meas_specification;
	u8                                 reserved01[3];
	__le32                             measurement_hash_algo;
	__le32                             reserved08[2];
};

struct mpi3_man_page17 {
	struct mpi3_config_page_header         header;
	__le32                             reserved08;
	u8                                 num_hash_algos;
	u8                                 reserved0d[3];
	struct mpi3_man17_hash_algorithm       hash_algorithm[MPI3_MAN17_HASH_ALGORITHM_MAX];
};

#define MPI3_MAN17_PAGEVERSION                                       (0x00)
struct mpi3_man_page20 {
	struct mpi3_config_page_header         header;
	__le32                             reserved08;
	__le32                             nonpremium_features;
	u8                                 allowed_personalities;
	u8                                 reserved11[3];
};

#define MPI3_MAN20_PAGEVERSION                                       (0x00)
#define MPI3_MAN20_ALLOWEDPERSON_RAID_MASK                           (0x02)
#define MPI3_MAN20_ALLOWEDPERSON_RAID_ALLOWED                        (0x02)
#define MPI3_MAN20_ALLOWEDPERSON_RAID_NOT_ALLOWED                    (0x00)
#define MPI3_MAN20_ALLOWEDPERSON_EHBA_MASK                           (0x01)
#define MPI3_MAN20_ALLOWEDPERSON_EHBA_ALLOWED                        (0x01)
#define MPI3_MAN20_ALLOWEDPERSON_EHBA_NOT_ALLOWED                    (0x00)
#define MPI3_MAN20_NONPREMUIM_DISABLE_PD_DEGRADED_MASK               (0x01)
#define MPI3_MAN20_NONPREMUIM_DISABLE_PD_DEGRADED_ENABLED            (0x00)
#define MPI3_MAN20_NONPREMUIM_DISABLE_PD_DEGRADED_DISABLED           (0x01)
struct mpi3_man_page21 {
	struct mpi3_config_page_header         header;
	__le32                             reserved08;
	__le32                             flags;
};

#define MPI3_MAN21_PAGEVERSION                                       (0x00)
#define MPI3_MAN21_FLAGS_HOST_METADATA_CAPABILITY_MASK               (0x80)
#define MPI3_MAN21_FLAGS_HOST_METADATA_CAPABILITY_ENABLED            (0x80)
#define MPI3_MAN21_FLAGS_HOST_METADATA_CAPABILITY_DISABLED           (0x00)
#define MPI3_MAN21_FLAGS_UNCERTIFIED_DRIVES_MASK                     (0x60)
#define MPI3_MAN21_FLAGS_UNCERTIFIED_DRIVES_BLOCK                    (0x00)
#define MPI3_MAN21_FLAGS_UNCERTIFIED_DRIVES_ALLOW                    (0x20)
#define MPI3_MAN21_FLAGS_UNCERTIFIED_DRIVES_WARN                     (0x40)
#define MPI3_MAN21_FLAGS_BLOCK_SSD_WR_CACHE_CHANGE_MASK              (0x08)
#define MPI3_MAN21_FLAGS_BLOCK_SSD_WR_CACHE_CHANGE_ALLOW             (0x00)
#define MPI3_MAN21_FLAGS_BLOCK_SSD_WR_CACHE_CHANGE_PREVENT           (0x08)
#define MPI3_MAN21_FLAGS_SES_VPD_ASSOC_MASK                          (0x01)
#define MPI3_MAN21_FLAGS_SES_VPD_ASSOC_DEFAULT                       (0x00)
#define MPI3_MAN21_FLAGS_SES_VPD_ASSOC_OEM_SPECIFIC                  (0x01)
#ifndef MPI3_MAN_PROD_SPECIFIC_MAX
#define MPI3_MAN_PROD_SPECIFIC_MAX                      (1)
#endif
struct mpi3_man_page_product_specific {
	struct mpi3_config_page_header         header;
	__le32                             product_specific_info[MPI3_MAN_PROD_SPECIFIC_MAX];
};

struct mpi3_io_unit_page0 {
	struct mpi3_config_page_header         header;
	__le64                             unique_value;
	__le32                             nvdata_version_default;
	__le32                             nvdata_version_persistent;
};

#define MPI3_IOUNIT0_PAGEVERSION                (0x00)
struct mpi3_io_unit_page1 {
	struct mpi3_config_page_header         header;
	__le32                             flags;
	u8                                 dmd_io_delay;
	u8                                 dmd_report_pcie;
	u8                                 dmd_report_sata;
	u8                                 dmd_report_sas;
};

#define MPI3_IOUNIT1_PAGEVERSION                (0x00)
#define MPI3_IOUNIT1_FLAGS_NVME_WRITE_CACHE_MASK                   (0x00000030)
#define MPI3_IOUNIT1_FLAGS_NVME_WRITE_CACHE_ENABLE                 (0x00000000)
#define MPI3_IOUNIT1_FLAGS_NVME_WRITE_CACHE_DISABLE                (0x00000010)
#define MPI3_IOUNIT1_FLAGS_NVME_WRITE_CACHE_NO_MODIFY              (0x00000020)
#define MPI3_IOUNIT1_FLAGS_ATA_SECURITY_FREEZE_LOCK                (0x00000008)
#define MPI3_IOUNIT1_FLAGS_WRITE_SAME_BUFFER                       (0x00000004)
#define MPI3_IOUNIT1_FLAGS_SATA_WRITE_CACHE_MASK                   (0x00000003)
#define MPI3_IOUNIT1_FLAGS_SATA_WRITE_CACHE_ENABLE                 (0x00000000)
#define MPI3_IOUNIT1_FLAGS_SATA_WRITE_CACHE_DISABLE                (0x00000001)
#define MPI3_IOUNIT1_FLAGS_SATA_WRITE_CACHE_UNCHANGED              (0x00000002)
#define MPI3_IOUNIT1_DMD_REPORT_DELAY_TIME_MASK                    (0x7f)
#define MPI3_IOUNIT1_DMD_REPORT_UNIT_16_SEC                        (0x80)
#ifndef MPI3_IO_UNIT2_GPIO_VAL_MAX
#define MPI3_IO_UNIT2_GPIO_VAL_MAX      (1)
#endif
struct mpi3_io_unit_page2 {
	struct mpi3_config_page_header         header;
	u8                                 gpio_count;
	u8                                 reserved09[3];
	__le16                             gpio_val[MPI3_IO_UNIT2_GPIO_VAL_MAX];
};

#define MPI3_IOUNIT2_PAGEVERSION                (0x00)
#define MPI3_IOUNIT2_GPIO_FUNCTION_MASK         (0xfffc)
#define MPI3_IOUNIT2_GPIO_FUNCTION_SHIFT        (2)
#define MPI3_IOUNIT2_GPIO_SETTING_MASK          (0x0001)
#define MPI3_IOUNIT2_GPIO_SETTING_OFF           (0x0000)
#define MPI3_IOUNIT2_GPIO_SETTING_ON            (0x0001)
struct mpi3_io_unit3_sensor {
	__le16             flags;
	u8                 threshold_margin;
	u8                 reserved03;
	__le16             threshold[3];
	__le16             reserved0a;
	__le32             reserved0c;
	__le32             reserved10;
	__le32             reserved14;
};

#define MPI3_IOUNIT3_SENSOR_FLAGS_FATAL_EVENT_ENABLED           (0x0010)
#define MPI3_IOUNIT3_SENSOR_FLAGS_FATAL_ACTION_ENABLED          (0x0008)
#define MPI3_IOUNIT3_SENSOR_FLAGS_CRITICAL_EVENT_ENABLED        (0x0004)
#define MPI3_IOUNIT3_SENSOR_FLAGS_CRITICAL_ACTION_ENABLED       (0x0002)
#define MPI3_IOUNIT3_SENSOR_FLAGS_WARNING_EVENT_ENABLED         (0x0001)
#ifndef MPI3_IO_UNIT3_SENSOR_MAX
#define MPI3_IO_UNIT3_SENSOR_MAX                                (1)
#endif
struct mpi3_io_unit_page3 {
	struct mpi3_config_page_header         header;
	__le32                             reserved08;
	u8                                 num_sensors;
	u8                                 nominal_poll_interval;
	u8                                 warning_poll_interval;
	u8                                 reserved0f;
	struct mpi3_io_unit3_sensor            sensor[MPI3_IO_UNIT3_SENSOR_MAX];
};

#define MPI3_IOUNIT3_PAGEVERSION                (0x00)
struct mpi3_io_unit4_sensor {
	__le16             current_temperature;
	__le16             reserved02;
	u8                 flags;
	u8                 reserved05[3];
	__le16             istwi_index;
	u8                 channel;
	u8                 reserved0b;
	__le32             reserved0c;
};

#define MPI3_IOUNIT4_SENSOR_FLAGS_LOC_MASK          (0xe0)
#define MPI3_IOUNIT4_SENSOR_FLAGS_LOC_SHIFT         (5)
#define MPI3_IOUNIT4_SENSOR_FLAGS_TEMP_VALID        (0x01)
#define MPI3_IOUNIT4_SENSOR_ISTWI_INDEX_INTERNAL    (0xffff)
#define MPI3_IOUNIT4_SENSOR_CHANNEL_RESERVED        (0xff)
#ifndef MPI3_IO_UNIT4_SENSOR_MAX
#define MPI3_IO_UNIT4_SENSOR_MAX                                (1)
#endif
struct mpi3_io_unit_page4 {
	struct mpi3_config_page_header         header;
	__le32                             reserved08;
	u8                                 num_sensors;
	u8                                 reserved0d[3];
	struct mpi3_io_unit4_sensor            sensor[MPI3_IO_UNIT4_SENSOR_MAX];
};

#define MPI3_IOUNIT4_PAGEVERSION                (0x00)
struct mpi3_io_unit5_spinup_group {
	u8                 max_target_spinup;
	u8                 spinup_delay;
	u8                 spinup_flags;
	u8                 reserved03;
};

#define MPI3_IOUNIT5_SPINUP_FLAGS_DISABLE       (0x01)
#ifndef MPI3_IO_UNIT5_PHY_MAX
#define MPI3_IO_UNIT5_PHY_MAX       (4)
#endif
struct mpi3_io_unit_page5 {
	struct mpi3_config_page_header         header;
	struct mpi3_io_unit5_spinup_group      spinup_group_parameters[4];
	__le32                             reserved18;
	__le32                             reserved1c;
	__le16                             device_shutdown;
	__le16                             reserved22;
	u8                                 pcie_device_wait_time;
	u8                                 sata_device_wait_time;
	u8                                 spinup_encl_drive_count;
	u8                                 spinup_encl_delay;
	u8                                 num_phys;
	u8                                 pe_initial_spinup_delay;
	u8                                 topology_stable_time;
	u8                                 flags;
	u8                                 phy[MPI3_IO_UNIT5_PHY_MAX];
};

#define MPI3_IOUNIT5_PAGEVERSION                           (0x00)
#define MPI3_IOUNIT5_DEVICE_SHUTDOWN_NO_ACTION             (0x00)
#define MPI3_IOUNIT5_DEVICE_SHUTDOWN_DIRECT_ATTACHED       (0x01)
#define MPI3_IOUNIT5_DEVICE_SHUTDOWN_EXPANDER_ATTACHED     (0x02)
#define MPI3_IOUNIT5_DEVICE_SHUTDOWN_SWITCH_ATTACHED       (0x02)
#define MPI3_IOUNIT5_DEVICE_SHUTDOWN_DIRECT_AND_EXPANDER   (0x03)
#define MPI3_IOUNIT5_DEVICE_SHUTDOWN_DIRECT_AND_SWITCH     (0x03)
#define MPI3_IOUNIT5_DEVICE_SHUTDOWN_SATA_HDD_MASK         (0x0300)
#define MPI3_IOUNIT5_DEVICE_SHUTDOWN_SATA_HDD_SHIFT        (8)
#define MPI3_IOUNIT5_DEVICE_SHUTDOWN_SAS_HDD_MASK          (0x00c0)
#define MPI3_IOUNIT5_DEVICE_SHUTDOWN_SAS_HDD_SHIFT         (6)
#define MPI3_IOUNIT5_DEVICE_SHUTDOWN_NVME_SSD_MASK         (0x0030)
#define MPI3_IOUNIT5_DEVICE_SHUTDOWN_NVME_SSD_SHIFT        (4)
#define MPI3_IOUNIT5_DEVICE_SHUTDOWN_SATA_SSD_MASK         (0x000c)
#define MPI3_IOUNIT5_DEVICE_SHUTDOWN_SATA_SSD_SHIFT        (2)
#define MPI3_IOUNIT5_DEVICE_SHUTDOWN_SAS_SSD_MASK          (0x0003)
#define MPI3_IOUNIT5_DEVICE_SHUTDOWN_SAA_SSD_SHIFT         (0)
#define MPI3_IOUNIT5_FLAGS_POWER_CAPABLE_SPINUP            (0x02)
#define MPI3_IOUNIT5_FLAGS_AUTO_PORT_ENABLE                (0x01)
#define MPI3_IOUNIT5_PHY_SPINUP_GROUP_MASK                 (0x03)
struct mpi3_io_unit_page6 {
	struct mpi3_config_page_header         header;
	__le32                             board_power_requirement;
	__le32                             pci_slot_power_allocation;
	u8                                 flags;
	u8                                 reserved11[3];
};

#define MPI3_IOUNIT6_PAGEVERSION                (0x00)
#define MPI3_IOUNIT6_FLAGS_ACT_CABLE_PWR_EXC    (0x01)
struct mpi3_io_unit_page7 {
	struct mpi3_config_page_header         header;
	__le32                             reserved08;
};

#define MPI3_IOUNIT7_PAGEVERSION                (0x00)
#ifndef MPI3_IOUNIT8_DIGEST_MAX
#define MPI3_IOUNIT8_DIGEST_MAX                   (1)
#endif
union mpi3_iounit8_digest {
	__le32                             dword[16];
	__le16                             word[32];
	u8                                 byte[64];
};

struct mpi3_io_unit_page8 {
	struct mpi3_config_page_header         header;
	u8                                 sb_mode;
	u8                                 sb_state;
	__le16                             reserved0a;
	u8                                 num_slots;
	u8                                 slots_available;
	u8                                 current_key_encryption_algo;
	u8                                 key_digest_hash_algo;
	__le32                             reserved10[2];
	__le32                             current_key[128];
	union mpi3_iounit8_digest             digest[MPI3_IOUNIT8_DIGEST_MAX];
};

#define MPI3_IOUNIT8_PAGEVERSION                  (0x00)
#define MPI3_IOUNIT8_SBMODE_SECURE_DEBUG          (0x04)
#define MPI3_IOUNIT8_SBMODE_HARD_SECURE           (0x02)
#define MPI3_IOUNIT8_SBMODE_CONFIG_SECURE         (0x01)
#define MPI3_IOUNIT8_SBSTATE_KEY_UPDATE_PENDING   (0x02)
#define MPI3_IOUNIT8_SBSTATE_SECURE_BOOT_ENABLED  (0x01)
struct mpi3_io_unit_page9 {
	struct mpi3_config_page_header         header;
	__le32                             flags;
	__le16                             first_device;
	__le16                             reserved0e;
};

#define MPI3_IOUNIT9_PAGEVERSION                  (0x00)
#define MPI3_IOUNIT9_FLAGS_VDFIRST_ENABLED         (0x01)
#define MPI3_IOUNIT9_FIRSTDEVICE_UNKNOWN          (0xffff)
struct mpi3_ioc_page0 {
	struct mpi3_config_page_header         header;
	__le32                             reserved08;
	__le16                             vendor_id;
	__le16                             device_id;
	u8                                 revision_id;
	u8                                 reserved11[3];
	__le32                             class_code;
	__le16                             subsystem_vendor_id;
	__le16                             subsystem_id;
};

#define MPI3_IOC0_PAGEVERSION               (0x00)
struct mpi3_ioc_page1 {
	struct mpi3_config_page_header         header;
	__le32                             coalescing_timeout;
	u8                                 coalescing_depth;
	u8                                 pci_slot_num;
	__le16                             reserved0e;
};

#define MPI3_IOC1_PAGEVERSION               (0x00)
#define MPI3_IOC1_PCISLOTNUM_UNKNOWN        (0xff)
#ifndef MPI3_IOC2_EVENTMASK_WORDS
#define MPI3_IOC2_EVENTMASK_WORDS           (4)
#endif
struct mpi3_ioc_page2 {
	struct mpi3_config_page_header         header;
	__le32                             reserved08;
	__le16                             sas_broadcast_primitive_masks;
	__le16                             sas_notify_primitive_masks;
	__le32                             event_masks[MPI3_IOC2_EVENTMASK_WORDS];
};

#define MPI3_IOC2_PAGEVERSION               (0x00)
#define MPI3_DRIVER_FLAGS_ADMINRAIDPD_BLOCKED               (0x0010)
#define MPI3_DRIVER_FLAGS_OOBRAIDPD_BLOCKED                 (0x0008)
#define MPI3_DRIVER_FLAGS_OOBRAIDVD_BLOCKED                 (0x0004)
#define MPI3_DRIVER_FLAGS_OOBADVHOSTPD_BLOCKED              (0x0002)
#define MPI3_DRIVER_FLAGS_OOBHOSTPD_BLOCKED                 (0x0001)
struct mpi3_allowed_cmd_scsi {
	__le16                             service_action;
	u8                                 operation_code;
	u8                                 command_flags;
};

struct mpi3_allowed_cmd_ata {
	u8                                 subcommand;
	u8                                 reserved01;
	u8                                 command;
	u8                                 command_flags;
};

struct mpi3_allowed_cmd_nvme {
	u8                                 reserved00;
	u8                                 nvme_cmd_flags;
	u8                                 op_code;
	u8                                 command_flags;
};

#define MPI3_DRIVER_ALLOWEDCMD_NVMECMDFLAGS_SUBQ_TYPE_MASK     (0x80)
#define MPI3_DRIVER_ALLOWEDCMD_NVMECMDFLAGS_SUBQ_TYPE_IO       (0x00)
#define MPI3_DRIVER_ALLOWEDCMD_NVMECMDFLAGS_SUBQ_TYPE_ADMIN    (0x80)
#define MPI3_DRIVER_ALLOWEDCMD_NVMECMDFLAGS_CMDSET_MASK        (0x3f)
#define MPI3_DRIVER_ALLOWEDCMD_NVMECMDFLAGS_CMDSET_NVM         (0x00)
union mpi3_allowed_cmd {
	struct mpi3_allowed_cmd_scsi           scsi;
	struct mpi3_allowed_cmd_ata            ata;
	struct mpi3_allowed_cmd_nvme           nvme;
};

#define MPI3_DRIVER_ALLOWEDCMD_CMDFLAGS_ADMINRAIDPD_BLOCKED    (0x20)
#define MPI3_DRIVER_ALLOWEDCMD_CMDFLAGS_OOBRAIDPD_BLOCKED      (0x10)
#define MPI3_DRIVER_ALLOWEDCMD_CMDFLAGS_OOBRAIDVD_BLOCKED      (0x08)
#define MPI3_DRIVER_ALLOWEDCMD_CMDFLAGS_OOBADVHOSTPD_BLOCKED   (0x04)
#define MPI3_DRIVER_ALLOWEDCMD_CMDFLAGS_OOBHOSTPD_BLOCKED      (0x02)
#define MPI3_DRIVER_ALLOWEDCMD_CMDFLAGS_CHECKSUBCMD_ENABLED    (0x01)
#ifndef MPI3_ALLOWED_CMDS_MAX
#define MPI3_ALLOWED_CMDS_MAX           (1)
#endif
struct mpi3_driver_page0 {
	struct mpi3_config_page_header         header;
	__le32                             bsd_options;
	u8                                 ssu_timeout;
	u8                                 io_timeout;
	u8                                 tur_retries;
	u8                                 tur_interval;
	u8                                 reserved10;
	u8                                 security_key_timeout;
	__le16                             reserved12;
	__le32                             reserved14;
	__le32                             reserved18;
};

#define MPI3_DRIVER0_PAGEVERSION               (0x00)
#define MPI3_DRIVER0_BSDOPTS_REGISTRATION_MASK              (0x00000003)
#define MPI3_DRIVER0_BSDOPTS_REGISTRATION_IOC_AND_DEVS      (0x00000000)
#define MPI3_DRIVER0_BSDOPTS_REGISTRATION_IOC_ONLY          (0x00000001)
#define MPI3_DRIVER0_BSDOPTS_DIS_HII_CONFIG_UTIL            (0x00000004)
#define MPI3_DRIVER0_BSDOPTS_EN_ADV_ADAPTER_CONFIG          (0x00000008)
struct mpi3_driver_page1 {
	struct mpi3_config_page_header         header;
	__le32                             flags;
	__le32                             reserved0c;
	__le16                             host_diag_trace_max_size;
	__le16                             host_diag_trace_min_size;
	__le16                             host_diag_trace_decrement_size;
	__le16                             reserved16;
	__le16                             host_diag_fw_max_size;
	__le16                             host_diag_fw_min_size;
	__le16                             host_diag_fw_decrement_size;
	__le16                             reserved1e;
	__le16                             host_diag_driver_max_size;
	__le16                             host_diag_driver_min_size;
	__le16                             host_diag_driver_decrement_size;
	__le16                             reserved26;
};

#define MPI3_DRIVER1_PAGEVERSION               (0x00)
#ifndef MPI3_DRIVER2_TRIGGER_MAX
#define MPI3_DRIVER2_TRIGGER_MAX           (1)
#endif
struct mpi3_driver2_trigger_event {
	u8                                 type;
	u8                                 flags;
	u8                                 reserved02;
	u8                                 event;
	__le32                             reserved04[3];
};

struct mpi3_driver2_trigger_scsi_sense {
	u8                                 type;
	u8                                 flags;
	__le16                             reserved02;
	u8                                 ascq;
	u8                                 asc;
	u8                                 sense_key;
	u8                                 reserved07;
	__le32                             reserved08[2];
};

#define MPI3_DRIVER2_TRIGGER_SCSI_SENSE_ASCQ_MATCH_ALL                        (0xff)
#define MPI3_DRIVER2_TRIGGER_SCSI_SENSE_ASC_MATCH_ALL                         (0xff)
#define MPI3_DRIVER2_TRIGGER_SCSI_SENSE_SENSE_KEY_MATCH_ALL                   (0xff)
struct mpi3_driver2_trigger_reply {
	u8                                 type;
	u8                                 flags;
	__le16                             ioc_status;
	__le32                             ioc_log_info;
	__le32                             ioc_log_info_mask;
	__le32                             reserved0c;
};

#define MPI3_DRIVER2_TRIGGER_REPLY_IOCSTATUS_MATCH_ALL                        (0xffff)
union mpi3_driver2_trigger_element {
	struct mpi3_driver2_trigger_event             event;
	struct mpi3_driver2_trigger_scsi_sense        scsi_sense;
	struct mpi3_driver2_trigger_reply             reply;
};

#define MPI3_DRIVER2_TRIGGER_TYPE_EVENT                                       (0x00)
#define MPI3_DRIVER2_TRIGGER_TYPE_SCSI_SENSE                                  (0x01)
#define MPI3_DRIVER2_TRIGGER_TYPE_REPLY                                       (0x02)
#define MPI3_DRIVER2_TRIGGER_FLAGS_DIAG_TRACE_RELEASE                         (0x02)
#define MPI3_DRIVER2_TRIGGER_FLAGS_DIAG_FW_RELEASE                            (0x01)
struct mpi3_driver_page2 {
	struct mpi3_config_page_header         header;
	__le64                             master_trigger;
	__le32                             reserved10[3];
	u8                                 num_triggers;
	u8                                 reserved1d[3];
	union mpi3_driver2_trigger_element    trigger[MPI3_DRIVER2_TRIGGER_MAX];
};

#define MPI3_DRIVER2_PAGEVERSION               (0x00)
#define MPI3_DRIVER2_MASTERTRIGGER_DIAG_TRACE_RELEASE                       (0x8000000000000000ULL)
#define MPI3_DRIVER2_MASTERTRIGGER_DIAG_FW_RELEASE                          (0x4000000000000000ULL)
#define MPI3_DRIVER2_MASTERTRIGGER_SNAPDUMP                                 (0x2000000000000000ULL)
#define MPI3_DRIVER2_MASTERTRIGGER_DEVICE_REMOVAL_ENABLED                   (0x0000000000000004ULL)
#define MPI3_DRIVER2_MASTERTRIGGER_TASK_MANAGEMENT_ENABLED                  (0x0000000000000002ULL)
struct mpi3_driver_page10 {
	struct mpi3_config_page_header         header;
	__le16                             flags;
	__le16                             reserved0a;
	u8                                 num_allowed_commands;
	u8                                 reserved0d[3];
	union mpi3_allowed_cmd                allowed_command[MPI3_ALLOWED_CMDS_MAX];
};

#define MPI3_DRIVER10_PAGEVERSION               (0x00)
struct mpi3_driver_page20 {
	struct mpi3_config_page_header         header;
	__le16                             flags;
	__le16                             reserved0a;
	u8                                 num_allowed_commands;
	u8                                 reserved0d[3];
	union mpi3_allowed_cmd                allowed_command[MPI3_ALLOWED_CMDS_MAX];
};

#define MPI3_DRIVER20_PAGEVERSION               (0x00)
struct mpi3_driver_page30 {
	struct mpi3_config_page_header         header;
	__le16                             flags;
	__le16                             reserved0a;
	u8                                 num_allowed_commands;
	u8                                 reserved0d[3];
	union mpi3_allowed_cmd                allowed_command[MPI3_ALLOWED_CMDS_MAX];
};

#define MPI3_DRIVER30_PAGEVERSION               (0x00)
union mpi3_security_mac {
	__le32                             dword[16];
	__le16                             word[32];
	u8                                 byte[64];
};

union mpi3_security_nonce {
	__le32                             dword[16];
	__le16                             word[32];
	u8                                 byte[64];
};

union mpi3_security0_cert_chain {
	__le32                             dword[1024];
	__le16                             word[2048];
	u8                                 byte[4096];
};

struct mpi3_security_page0 {
	struct mpi3_config_page_header         header;
	u8                                 slot_num_group;
	u8                                 slot_num;
	__le16                             cert_chain_length;
	u8                                 cert_chain_flags;
	u8                                 reserved0d[3];
	__le32                             base_asym_algo;
	__le32                             base_hash_algo;
	__le32                             reserved18[4];
	union mpi3_security_mac               mac;
	union mpi3_security_nonce             nonce;
	union mpi3_security0_cert_chain       certificate_chain;
};

#define MPI3_SECURITY0_PAGEVERSION               (0x00)
#define MPI3_SECURITY0_CERTCHAIN_FLAGS_AUTH_API_MASK       (0x0e)
#define MPI3_SECURITY0_CERTCHAIN_FLAGS_AUTH_API_UNUSED     (0x00)
#define MPI3_SECURITY0_CERTCHAIN_FLAGS_AUTH_API_CERBERUS   (0x02)
#define MPI3_SECURITY0_CERTCHAIN_FLAGS_AUTH_API_SPDM       (0x04)
#define MPI3_SECURITY0_CERTCHAIN_FLAGS_SEALED              (0x01)
#ifndef MPI3_SECURITY1_KEY_RECORD_MAX
#define MPI3_SECURITY1_KEY_RECORD_MAX      1
#endif
#ifndef MPI3_SECURITY1_PAD_MAX
#define MPI3_SECURITY1_PAD_MAX      1
#endif
union mpi3_security1_key_data {
	__le32                             dword[128];
	__le16                             word[256];
	u8                                 byte[512];
};

struct mpi3_security1_key_record {
	u8                                 flags;
	u8                                 consumer;
	__le16                             key_data_size;
	__le32                             additional_key_data;
	__le32                             reserved08[2];
	union mpi3_security1_key_data         key_data;
};

#define MPI3_SECURITY1_KEY_RECORD_FLAGS_TYPE_MASK            (0x1f)
#define MPI3_SECURITY1_KEY_RECORD_FLAGS_TYPE_NOT_VALID       (0x00)
#define MPI3_SECURITY1_KEY_RECORD_FLAGS_TYPE_HMAC            (0x01)
#define MPI3_SECURITY1_KEY_RECORD_FLAGS_TYPE_AES             (0x02)
#define MPI3_SECURITY1_KEY_RECORD_FLAGS_TYPE_ECDSA_PRIVATE   (0x03)
#define MPI3_SECURITY1_KEY_RECORD_FLAGS_TYPE_ECDSA_PUBLIC    (0x04)
#define MPI3_SECURITY1_KEY_RECORD_CONSUMER_NOT_VALID         (0x00)
#define MPI3_SECURITY1_KEY_RECORD_CONSUMER_SAFESTORE         (0x01)
#define MPI3_SECURITY1_KEY_RECORD_CONSUMER_CERT_CHAIN        (0x02)
#define MPI3_SECURITY1_KEY_RECORD_CONSUMER_DEVICE_KEY        (0x03)
#define MPI3_SECURITY1_KEY_RECORD_CONSUMER_CACHE_OFFLOAD     (0x04)
struct mpi3_security_page1 {
	struct mpi3_config_page_header         header;
	__le32                             reserved08[2];
	union mpi3_security_mac               mac;
	union mpi3_security_nonce             nonce;
	u8                                 num_keys;
	u8                                 reserved91[3];
	__le32                             reserved94[3];
	struct mpi3_security1_key_record       key_record[MPI3_SECURITY1_KEY_RECORD_MAX];
	u8                                 pad[MPI3_SECURITY1_PAD_MAX];
};

#define MPI3_SECURITY1_PAGEVERSION               (0x00)
struct mpi3_sas_io_unit0_phy_data {
	u8                 io_unit_port;
	u8                 port_flags;
	u8                 phy_flags;
	u8                 negotiated_link_rate;
	__le16             controller_phy_device_info;
	__le16             reserved06;
	__le16             attached_dev_handle;
	__le16             controller_dev_handle;
	__le32             discovery_status;
	__le32             reserved10;
};

#ifndef MPI3_SAS_IO_UNIT0_PHY_MAX
#define MPI3_SAS_IO_UNIT0_PHY_MAX           (1)
#endif
struct mpi3_sas_io_unit_page0 {
	struct mpi3_config_page_header         header;
	__le32                             reserved08;
	u8                                 num_phys;
	u8                                 init_status;
	__le16                             reserved0e;
	struct mpi3_sas_io_unit0_phy_data      phy_data[MPI3_SAS_IO_UNIT0_PHY_MAX];
};

#define MPI3_SASIOUNIT0_PAGEVERSION                          (0x00)
#define MPI3_SASIOUNIT0_INITSTATUS_NO_ERRORS                 (0x00)
#define MPI3_SASIOUNIT0_INITSTATUS_NEEDS_INITIALIZATION      (0x01)
#define MPI3_SASIOUNIT0_INITSTATUS_NO_TARGETS_ALLOCATED      (0x02)
#define MPI3_SASIOUNIT0_INITSTATUS_BAD_NUM_PHYS              (0x04)
#define MPI3_SASIOUNIT0_INITSTATUS_UNSUPPORTED_CONFIG        (0x05)
#define MPI3_SASIOUNIT0_INITSTATUS_HOST_PHYS_ENABLED         (0x06)
#define MPI3_SASIOUNIT0_INITSTATUS_PRODUCT_SPECIFIC_MIN      (0xf0)
#define MPI3_SASIOUNIT0_INITSTATUS_PRODUCT_SPECIFIC_MAX      (0xff)
#define MPI3_SASIOUNIT0_PORTFLAGS_DISC_IN_PROGRESS           (0x08)
#define MPI3_SASIOUNIT0_PORTFLAGS_AUTO_PORT_CONFIG_MASK      (0x03)
#define MPI3_SASIOUNIT0_PORTFLAGS_AUTO_PORT_CONFIG_IOUNIT1   (0x00)
#define MPI3_SASIOUNIT0_PORTFLAGS_AUTO_PORT_CONFIG_DYNAMIC   (0x01)
#define MPI3_SASIOUNIT0_PORTFLAGS_AUTO_PORT_CONFIG_BACKPLANE (0x02)
#define MPI3_SASIOUNIT0_PHYFLAGS_INIT_PERSIST_CONNECT        (0x40)
#define MPI3_SASIOUNIT0_PHYFLAGS_TARG_PERSIST_CONNECT        (0x20)
#define MPI3_SASIOUNIT0_PHYFLAGS_PHY_DISABLED                (0x08)
#define MPI3_SASIOUNIT0_PHYFLAGS_VIRTUAL_PHY                 (0x02)
#define MPI3_SASIOUNIT0_PHYFLAGS_HOST_PHY                    (0x01)
struct mpi3_sas_io_unit1_phy_data {
	u8                 io_unit_port;
	u8                 port_flags;
	u8                 phy_flags;
	u8                 max_min_link_rate;
	__le16             controller_phy_device_info;
	__le16             max_target_port_connect_time;
	__le32             reserved08;
};

#ifndef MPI3_SAS_IO_UNIT1_PHY_MAX
#define MPI3_SAS_IO_UNIT1_PHY_MAX           (1)
#endif
struct mpi3_sas_io_unit_page1 {
	struct mpi3_config_page_header         header;
	__le16                             control_flags;
	__le16                             sas_narrow_max_queue_depth;
	__le16                             additional_control_flags;
	__le16                             sas_wide_max_queue_depth;
	u8                                 num_phys;
	u8                                 sata_max_q_depth;
	__le16                             reserved12;
	struct mpi3_sas_io_unit1_phy_data      phy_data[MPI3_SAS_IO_UNIT1_PHY_MAX];
};

#define MPI3_SASIOUNIT1_PAGEVERSION                                 (0x00)
#define MPI3_SASIOUNIT1_CONTROL_CONTROLLER_DEVICE_SELF_TEST         (0x8000)
#define MPI3_SASIOUNIT1_CONTROL_SATA_SW_PRESERVE                    (0x1000)
#define MPI3_SASIOUNIT1_CONTROL_SATA_48BIT_LBA_REQUIRED             (0x0080)
#define MPI3_SASIOUNIT1_CONTROL_SATA_SMART_REQUIRED                 (0x0040)
#define MPI3_SASIOUNIT1_CONTROL_SATA_NCQ_REQUIRED                   (0x0020)
#define MPI3_SASIOUNIT1_CONTROL_SATA_FUA_REQUIRED                   (0x0010)
#define MPI3_SASIOUNIT1_CONTROL_TABLE_SUBTRACTIVE_ILLEGAL           (0x0008)
#define MPI3_SASIOUNIT1_CONTROL_SUBTRACTIVE_ILLEGAL                 (0x0004)
#define MPI3_SASIOUNIT1_CONTROL_FIRST_LVL_DISC_ONLY                 (0x0002)
#define MPI3_SASIOUNIT1_CONTROL_HARD_RESET_MASK                     (0x0001)
#define MPI3_SASIOUNIT1_CONTROL_HARD_RESET_DEVICE_NAME              (0x0000)
#define MPI3_SASIOUNIT1_CONTROL_HARD_RESET_SAS_ADDRESS              (0x0001)
#define MPI3_SASIOUNIT1_ACONTROL_DA_PERSIST_CONNECT                 (0x0100)
#define MPI3_SASIOUNIT1_ACONTROL_MULTI_PORT_DOMAIN_ILLEGAL          (0x0080)
#define MPI3_SASIOUNIT1_ACONTROL_SATA_ASYNCHROUNOUS_NOTIFICATION    (0x0040)
#define MPI3_SASIOUNIT1_ACONTROL_INVALID_TOPOLOGY_CORRECTION        (0x0020)
#define MPI3_SASIOUNIT1_ACONTROL_PORT_ENABLE_ONLY_SATA_LINK_RESET   (0x0010)
#define MPI3_SASIOUNIT1_ACONTROL_OTHER_AFFILIATION_SATA_LINK_RESET  (0x0008)
#define MPI3_SASIOUNIT1_ACONTROL_SELF_AFFILIATION_SATA_LINK_RESET   (0x0004)
#define MPI3_SASIOUNIT1_ACONTROL_NO_AFFILIATION_SATA_LINK_RESET     (0x0002)
#define MPI3_SASIOUNIT1_ACONTROL_ALLOW_TABLE_TO_TABLE               (0x0001)
#define MPI3_SASIOUNIT1_PORT_FLAGS_AUTO_PORT_CONFIG                 (0x01)
#define MPI3_SASIOUNIT1_PHYFLAGS_INIT_PERSIST_CONNECT               (0x40)
#define MPI3_SASIOUNIT1_PHYFLAGS_TARG_PERSIST_CONNECT               (0x20)
#define MPI3_SASIOUNIT1_PHYFLAGS_PHY_DISABLE                        (0x08)
#define MPI3_SASIOUNIT1_MMLR_MAX_RATE_MASK                          (0xf0)
#define MPI3_SASIOUNIT1_MMLR_MAX_RATE_SHIFT                         (4)
#define MPI3_SASIOUNIT1_MMLR_MAX_RATE_6_0                           (0xa0)
#define MPI3_SASIOUNIT1_MMLR_MAX_RATE_12_0                          (0xb0)
#define MPI3_SASIOUNIT1_MMLR_MAX_RATE_22_5                          (0xc0)
#define MPI3_SASIOUNIT1_MMLR_MIN_RATE_MASK                          (0x0f)
#define MPI3_SASIOUNIT1_MMLR_MIN_RATE_6_0                           (0x0a)
#define MPI3_SASIOUNIT1_MMLR_MIN_RATE_12_0                          (0x0b)
#define MPI3_SASIOUNIT1_MMLR_MIN_RATE_22_5                          (0x0c)
struct mpi3_sas_io_unit2_phy_pm_settings {
	u8                 control_flags;
	u8                 reserved01;
	__le16             inactivity_timer_exponent;
	u8                 sata_partial_timeout;
	u8                 reserved05;
	u8                 sata_slumber_timeout;
	u8                 reserved07;
	u8                 sas_partial_timeout;
	u8                 reserved09;
	u8                 sas_slumber_timeout;
	u8                 reserved0b;
};

#ifndef MPI3_SAS_IO_UNIT2_PHY_MAX
#define MPI3_SAS_IO_UNIT2_PHY_MAX           (1)
#endif
struct mpi3_sas_io_unit_page2 {
	struct mpi3_config_page_header             header;
	u8                                     num_phys;
	u8                                     reserved09[3];
	__le32                                 reserved0c;
	struct mpi3_sas_io_unit2_phy_pm_settings   sas_phy_power_management_settings[MPI3_SAS_IO_UNIT2_PHY_MAX];
};

#define MPI3_SASIOUNIT2_PAGEVERSION                     (0x00)
#define MPI3_SASIOUNIT2_CONTROL_SAS_SLUMBER_ENABLE      (0x08)
#define MPI3_SASIOUNIT2_CONTROL_SAS_PARTIAL_ENABLE      (0x04)
#define MPI3_SASIOUNIT2_CONTROL_SATA_SLUMBER_ENABLE     (0x02)
#define MPI3_SASIOUNIT2_CONTROL_SATA_PARTIAL_ENABLE     (0x01)
#define MPI3_SASIOUNIT2_ITE_SAS_SLUMBER_MASK            (0x7000)
#define MPI3_SASIOUNIT2_ITE_SAS_SLUMBER_SHIFT           (12)
#define MPI3_SASIOUNIT2_ITE_SAS_PARTIAL_MASK            (0x0700)
#define MPI3_SASIOUNIT2_ITE_SAS_PARTIAL_SHIFT           (8)
#define MPI3_SASIOUNIT2_ITE_SATA_SLUMBER_MASK           (0x0070)
#define MPI3_SASIOUNIT2_ITE_SATA_SLUMBER_SHIFT          (4)
#define MPI3_SASIOUNIT2_ITE_SATA_PARTIAL_MASK           (0x0007)
#define MPI3_SASIOUNIT2_ITE_SATA_PARTIAL_SHIFT          (0)
#define MPI3_SASIOUNIT2_ITE_EXP_TEN_SECONDS             (7)
#define MPI3_SASIOUNIT2_ITE_EXP_ONE_SECOND              (6)
#define MPI3_SASIOUNIT2_ITE_EXP_HUNDRED_MILLISECONDS    (5)
#define MPI3_SASIOUNIT2_ITE_EXP_TEN_MILLISECONDS        (4)
#define MPI3_SASIOUNIT2_ITE_EXP_ONE_MILLISECOND         (3)
#define MPI3_SASIOUNIT2_ITE_EXP_HUNDRED_MICROSECONDS    (2)
#define MPI3_SASIOUNIT2_ITE_EXP_TEN_MICROSECONDS        (1)
#define MPI3_SASIOUNIT2_ITE_EXP_ONE_MICROSECOND         (0)
struct mpi3_sas_io_unit_page3 {
	struct mpi3_config_page_header         header;
	__le32                             reserved08;
	__le32                             power_management_capabilities;
};

#define MPI3_SASIOUNIT3_PAGEVERSION                     (0x00)
#define MPI3_SASIOUNIT3_PM_HOST_SAS_SLUMBER_MODE        (0x00000800)
#define MPI3_SASIOUNIT3_PM_HOST_SAS_PARTIAL_MODE        (0x00000400)
#define MPI3_SASIOUNIT3_PM_HOST_SATA_SLUMBER_MODE       (0x00000200)
#define MPI3_SASIOUNIT3_PM_HOST_SATA_PARTIAL_MODE       (0x00000100)
#define MPI3_SASIOUNIT3_PM_IOUNIT_SAS_SLUMBER_MODE      (0x00000008)
#define MPI3_SASIOUNIT3_PM_IOUNIT_SAS_PARTIAL_MODE      (0x00000004)
#define MPI3_SASIOUNIT3_PM_IOUNIT_SATA_SLUMBER_MODE     (0x00000002)
#define MPI3_SASIOUNIT3_PM_IOUNIT_SATA_PARTIAL_MODE     (0x00000001)
struct mpi3_sas_expander_page0 {
	struct mpi3_config_page_header         header;
	u8                                 io_unit_port;
	u8                                 report_gen_length;
	__le16                             enclosure_handle;
	__le32                             reserved0c;
	__le64                             sas_address;
	__le32                             discovery_status;
	__le16                             dev_handle;
	__le16                             parent_dev_handle;
	__le16                             expander_change_count;
	__le16                             expander_route_indexes;
	u8                                 num_phys;
	u8                                 sas_level;
	__le16                             flags;
	__le16                             stp_bus_inactivity_time_limit;
	__le16                             stp_max_connect_time_limit;
	__le16                             stp_smp_nexus_loss_time;
	__le16                             max_num_routed_sas_addresses;
	__le64                             active_zone_manager_sas_address;
	__le16                             zone_lock_inactivity_limit;
	__le16                             reserved3a;
	u8                                 time_to_reduced_func;
	u8                                 initial_time_to_reduced_func;
	u8                                 max_reduced_func_time;
	u8                                 exp_status;
};

#define MPI3_SASEXPANDER0_PAGEVERSION                       (0x00)
#define MPI3_SASEXPANDER0_FLAGS_REDUCED_FUNCTIONALITY       (0x2000)
#define MPI3_SASEXPANDER0_FLAGS_ZONE_LOCKED                 (0x1000)
#define MPI3_SASEXPANDER0_FLAGS_SUPPORTED_PHYSICAL_PRES     (0x0800)
#define MPI3_SASEXPANDER0_FLAGS_ASSERTED_PHYSICAL_PRES      (0x0400)
#define MPI3_SASEXPANDER0_FLAGS_ZONING_SUPPORT              (0x0200)
#define MPI3_SASEXPANDER0_FLAGS_ENABLED_ZONING              (0x0100)
#define MPI3_SASEXPANDER0_FLAGS_TABLE_TO_TABLE_SUPPORT      (0x0080)
#define MPI3_SASEXPANDER0_FLAGS_CONNECTOR_END_DEVICE        (0x0010)
#define MPI3_SASEXPANDER0_FLAGS_OTHERS_CONFIG               (0x0004)
#define MPI3_SASEXPANDER0_FLAGS_CONFIG_IN_PROGRESS          (0x0002)
#define MPI3_SASEXPANDER0_FLAGS_ROUTE_TABLE_CONFIG          (0x0001)
#define MPI3_SASEXPANDER0_ES_NOT_RESPONDING                 (0x02)
#define MPI3_SASEXPANDER0_ES_RESPONDING                     (0x03)
#define MPI3_SASEXPANDER0_ES_DELAY_NOT_RESPONDING           (0x04)
struct mpi3_sas_expander_page1 {
	struct mpi3_config_page_header         header;
	u8                                 io_unit_port;
	u8                                 reserved09[3];
	u8                                 num_phys;
	u8                                 phy;
	__le16                             num_table_entries_programmed;
	u8                                 programmed_link_rate;
	u8                                 hw_link_rate;
	__le16                             attached_dev_handle;
	__le32                             phy_info;
	__le16                             attached_device_info;
	__le16                             reserved1a;
	__le16                             expander_dev_handle;
	u8                                 change_count;
	u8                                 negotiated_link_rate;
	u8                                 phy_identifier;
	u8                                 attached_phy_identifier;
	u8                                 reserved22;
	u8                                 discovery_info;
	__le32                             attached_phy_info;
	u8                                 zone_group;
	u8                                 self_config_status;
	__le16                             reserved2a;
	__le16                             slot;
	__le16                             slot_index;
};

#define MPI3_SASEXPANDER1_PAGEVERSION                   (0x00)
#define MPI3_SASEXPANDER1_DISCINFO_BAD_PHY_DISABLED     (0x04)
#define MPI3_SASEXPANDER1_DISCINFO_LINK_STATUS_CHANGE   (0x02)
#define MPI3_SASEXPANDER1_DISCINFO_NO_ROUTING_ENTRIES   (0x01)
#ifndef MPI3_SASEXPANDER2_MAX_NUM_PHYS
#define MPI3_SASEXPANDER2_MAX_NUM_PHYS                               (1)
#endif
struct mpi3_sasexpander2_phy_element {
	u8                                 link_change_count;
	u8                                 reserved01;
	__le16                             rate_change_count;
	__le32                             reserved04;
};

struct mpi3_sas_expander_page2 {
	struct mpi3_config_page_header         header;
	u8                                 num_phys;
	u8                                 reserved09;
	__le16                             dev_handle;
	__le32                             reserved0c;
	struct mpi3_sasexpander2_phy_element   phy[MPI3_SASEXPANDER2_MAX_NUM_PHYS];
};

#define MPI3_SASEXPANDER2_PAGEVERSION                   (0x00)
struct mpi3_sas_port_page0 {
	struct mpi3_config_page_header         header;
	u8                                 port_number;
	u8                                 reserved09;
	u8                                 port_width;
	u8                                 reserved0b;
	u8                                 zone_group;
	u8                                 reserved0d[3];
	__le64                             sas_address;
	__le16                             device_info;
	__le16                             reserved1a;
	__le32                             reserved1c;
};

#define MPI3_SASPORT0_PAGEVERSION                       (0x00)
struct mpi3_sas_phy_page0 {
	struct mpi3_config_page_header         header;
	__le16                             owner_dev_handle;
	__le16                             reserved0a;
	__le16                             attached_dev_handle;
	u8                                 attached_phy_identifier;
	u8                                 reserved0f;
	__le32                             attached_phy_info;
	u8                                 programmed_link_rate;
	u8                                 hw_link_rate;
	u8                                 change_count;
	u8                                 flags;
	__le32                             phy_info;
	u8                                 negotiated_link_rate;
	u8                                 reserved1d[3];
	__le16                             slot;
	__le16                             slot_index;
};

#define MPI3_SASPHY0_PAGEVERSION                        (0x00)
#define MPI3_SASPHY0_FLAGS_SGPIO_DIRECT_ATTACH_ENC      (0x01)
struct mpi3_sas_phy_page1 {
	struct mpi3_config_page_header         header;
	__le32                             reserved08;
	__le32                             invalid_dword_count;
	__le32                             running_disparity_error_count;
	__le32                             loss_dword_synch_count;
	__le32                             phy_reset_problem_count;
};

#define MPI3_SASPHY1_PAGEVERSION                        (0x00)
struct mpi3_sas_phy2_phy_event {
	u8         phy_event_code;
	u8         reserved01[3];
	__le32     phy_event_info;
};

#ifndef MPI3_SAS_PHY2_PHY_EVENT_MAX
#define MPI3_SAS_PHY2_PHY_EVENT_MAX         (1)
#endif
struct mpi3_sas_phy_page2 {
	struct mpi3_config_page_header         header;
	__le32                             reserved08;
	u8                                 num_phy_events;
	u8                                 reserved0d[3];
	struct mpi3_sas_phy2_phy_event         phy_event[MPI3_SAS_PHY2_PHY_EVENT_MAX];
};

#define MPI3_SASPHY2_PAGEVERSION                        (0x00)
struct mpi3_sas_phy3_phy_event_config {
	u8         phy_event_code;
	u8         reserved01[3];
	u8         counter_type;
	u8         threshold_window;
	u8         time_units;
	u8         reserved07;
	__le32     event_threshold;
	__le16     threshold_flags;
	__le16     reserved0e;
};

#define MPI3_SASPHY3_EVENT_CODE_NO_EVENT                    (0x00)
#define MPI3_SASPHY3_EVENT_CODE_INVALID_DWORD               (0x01)
#define MPI3_SASPHY3_EVENT_CODE_RUNNING_DISPARITY_ERROR     (0x02)
#define MPI3_SASPHY3_EVENT_CODE_LOSS_DWORD_SYNC             (0x03)
#define MPI3_SASPHY3_EVENT_CODE_PHY_RESET_PROBLEM           (0x04)
#define MPI3_SASPHY3_EVENT_CODE_ELASTICITY_BUF_OVERFLOW     (0x05)
#define MPI3_SASPHY3_EVENT_CODE_RX_ERROR                    (0x06)
#define MPI3_SASPHY3_EVENT_CODE_INV_SPL_PACKETS             (0x07)
#define MPI3_SASPHY3_EVENT_CODE_LOSS_SPL_PACKET_SYNC        (0x08)
#define MPI3_SASPHY3_EVENT_CODE_RX_ADDR_FRAME_ERROR         (0x20)
#define MPI3_SASPHY3_EVENT_CODE_TX_AC_OPEN_REJECT           (0x21)
#define MPI3_SASPHY3_EVENT_CODE_RX_AC_OPEN_REJECT           (0x22)
#define MPI3_SASPHY3_EVENT_CODE_TX_RC_OPEN_REJECT           (0x23)
#define MPI3_SASPHY3_EVENT_CODE_RX_RC_OPEN_REJECT           (0x24)
#define MPI3_SASPHY3_EVENT_CODE_RX_AIP_PARTIAL_WAITING_ON   (0x25)
#define MPI3_SASPHY3_EVENT_CODE_RX_AIP_CONNECT_WAITING_ON   (0x26)
#define MPI3_SASPHY3_EVENT_CODE_TX_BREAK                    (0x27)
#define MPI3_SASPHY3_EVENT_CODE_RX_BREAK                    (0x28)
#define MPI3_SASPHY3_EVENT_CODE_BREAK_TIMEOUT               (0x29)
#define MPI3_SASPHY3_EVENT_CODE_CONNECTION                  (0x2a)
#define MPI3_SASPHY3_EVENT_CODE_PEAKTX_PATHWAY_BLOCKED      (0x2b)
#define MPI3_SASPHY3_EVENT_CODE_PEAKTX_ARB_WAIT_TIME        (0x2c)
#define MPI3_SASPHY3_EVENT_CODE_PEAK_ARB_WAIT_TIME          (0x2d)
#define MPI3_SASPHY3_EVENT_CODE_PEAK_CONNECT_TIME           (0x2e)
#define MPI3_SASPHY3_EVENT_CODE_PERSIST_CONN                (0x2f)
#define MPI3_SASPHY3_EVENT_CODE_TX_SSP_FRAMES               (0x40)
#define MPI3_SASPHY3_EVENT_CODE_RX_SSP_FRAMES               (0x41)
#define MPI3_SASPHY3_EVENT_CODE_TX_SSP_ERROR_FRAMES         (0x42)
#define MPI3_SASPHY3_EVENT_CODE_RX_SSP_ERROR_FRAMES         (0x43)
#define MPI3_SASPHY3_EVENT_CODE_TX_CREDIT_BLOCKED           (0x44)
#define MPI3_SASPHY3_EVENT_CODE_RX_CREDIT_BLOCKED           (0x45)
#define MPI3_SASPHY3_EVENT_CODE_TX_SATA_FRAMES              (0x50)
#define MPI3_SASPHY3_EVENT_CODE_RX_SATA_FRAMES              (0x51)
#define MPI3_SASPHY3_EVENT_CODE_SATA_OVERFLOW               (0x52)
#define MPI3_SASPHY3_EVENT_CODE_TX_SMP_FRAMES               (0x60)
#define MPI3_SASPHY3_EVENT_CODE_RX_SMP_FRAMES               (0x61)
#define MPI3_SASPHY3_EVENT_CODE_RX_SMP_ERROR_FRAMES         (0x63)
#define MPI3_SASPHY3_EVENT_CODE_HOTPLUG_TIMEOUT             (0xd0)
#define MPI3_SASPHY3_EVENT_CODE_MISALIGNED_MUX_PRIMITIVE    (0xd1)
#define MPI3_SASPHY3_EVENT_CODE_RX_AIP                      (0xd2)
#define MPI3_SASPHY3_EVENT_CODE_LCARB_WAIT_TIME             (0xd3)
#define MPI3_SASPHY3_EVENT_CODE_RCVD_CONN_RESP_WAIT_TIME    (0xd4)
#define MPI3_SASPHY3_EVENT_CODE_LCCONN_TIME                 (0xd5)
#define MPI3_SASPHY3_EVENT_CODE_SSP_TX_START_TRANSMIT       (0xd6)
#define MPI3_SASPHY3_EVENT_CODE_SATA_TX_START               (0xd7)
#define MPI3_SASPHY3_EVENT_CODE_SMP_TX_START_TRANSMT        (0xd8)
#define MPI3_SASPHY3_EVENT_CODE_TX_SMP_BREAK_CONN           (0xd9)
#define MPI3_SASPHY3_EVENT_CODE_SSP_RX_START_RECEIVE        (0xda)
#define MPI3_SASPHY3_EVENT_CODE_SATA_RX_START_RECEIVE       (0xdb)
#define MPI3_SASPHY3_EVENT_CODE_SMP_RX_START_RECEIVE        (0xdc)
#define MPI3_SASPHY3_COUNTER_TYPE_WRAPPING                  (0x00)
#define MPI3_SASPHY3_COUNTER_TYPE_SATURATING                (0x01)
#define MPI3_SASPHY3_COUNTER_TYPE_PEAK_VALUE                (0x02)
#define MPI3_SASPHY3_TIME_UNITS_10_MICROSECONDS             (0x00)
#define MPI3_SASPHY3_TIME_UNITS_100_MICROSECONDS            (0x01)
#define MPI3_SASPHY3_TIME_UNITS_1_MILLISECOND               (0x02)
#define MPI3_SASPHY3_TIME_UNITS_10_MILLISECONDS             (0x03)
#define MPI3_SASPHY3_TFLAGS_PHY_RESET                       (0x0002)
#define MPI3_SASPHY3_TFLAGS_EVENT_NOTIFY                    (0x0001)
#ifndef MPI3_SAS_PHY3_PHY_EVENT_MAX
#define MPI3_SAS_PHY3_PHY_EVENT_MAX         (1)
#endif
struct mpi3_sas_phy_page3 {
	struct mpi3_config_page_header         header;
	__le32                             reserved08;
	u8                                 num_phy_events;
	u8                                 reserved0d[3];
	struct mpi3_sas_phy3_phy_event_config  phy_event_config[MPI3_SAS_PHY3_PHY_EVENT_MAX];
};

#define MPI3_SASPHY3_PAGEVERSION                        (0x00)
struct mpi3_sas_phy_page4 {
	struct mpi3_config_page_header         header;
	u8                                 reserved08[3];
	u8                                 flags;
	u8                                 initial_frame[28];
};

#define MPI3_SASPHY4_PAGEVERSION                        (0x00)
#define MPI3_SASPHY4_FLAGS_FRAME_VALID                  (0x02)
#define MPI3_SASPHY4_FLAGS_SATA_FRAME                   (0x01)
#define MPI3_PCIE_LINK_RETIMERS_MASK                    (0x30)
#define MPI3_PCIE_LINK_RETIMERS_SHIFT                   (4)
#define MPI3_PCIE_NEG_LINK_RATE_MASK                    (0x0f)
#define MPI3_PCIE_NEG_LINK_RATE_UNKNOWN                 (0x00)
#define MPI3_PCIE_NEG_LINK_RATE_PHY_DISABLED            (0x01)
#define MPI3_PCIE_NEG_LINK_RATE_2_5                     (0x02)
#define MPI3_PCIE_NEG_LINK_RATE_5_0                     (0x03)
#define MPI3_PCIE_NEG_LINK_RATE_8_0                     (0x04)
#define MPI3_PCIE_NEG_LINK_RATE_16_0                    (0x05)
#define MPI3_PCIE_NEG_LINK_RATE_32_0                    (0x06)
#define MPI3_PCIE_ASPM_ENABLE_NONE                      (0x0)
#define MPI3_PCIE_ASPM_ENABLE_L0S                       (0x1)
#define MPI3_PCIE_ASPM_ENABLE_L1                        (0x2)
#define MPI3_PCIE_ASPM_ENABLE_L0S_L1                    (0x3)
#define MPI3_PCIE_ASPM_SUPPORT_NONE                     (0x0)
#define MPI3_PCIE_ASPM_SUPPORT_L0S                      (0x1)
#define MPI3_PCIE_ASPM_SUPPORT_L1                       (0x2)
#define MPI3_PCIE_ASPM_SUPPORT_L0S_L1                   (0x3)
struct mpi3_pcie_io_unit0_phy_data {
	u8         link;
	u8         link_flags;
	u8         phy_flags;
	u8         negotiated_link_rate;
	__le16     attached_dev_handle;
	__le16     controller_dev_handle;
	__le32     enumeration_status;
	u8         io_unit_port;
	u8         reserved0d[3];
};

#define MPI3_PCIEIOUNIT0_LINKFLAGS_CONFIG_SOURCE_MASK      (0x10)
#define MPI3_PCIEIOUNIT0_LINKFLAGS_CONFIG_SOURCE_IOUNIT1   (0x00)
#define MPI3_PCIEIOUNIT0_LINKFLAGS_CONFIG_SOURCE_BKPLANE   (0x10)
#define MPI3_PCIEIOUNIT0_LINKFLAGS_ENUM_IN_PROGRESS        (0x08)
#define MPI3_PCIEIOUNIT0_PHYFLAGS_PHY_DISABLED          (0x08)
#define MPI3_PCIEIOUNIT0_PHYFLAGS_HOST_PHY              (0x01)
#define MPI3_PCIEIOUNIT0_ES_MAX_SWITCH_DEPTH_EXCEEDED   (0x80000000)
#define MPI3_PCIEIOUNIT0_ES_MAX_SWITCHES_EXCEEDED       (0x40000000)
#define MPI3_PCIEIOUNIT0_ES_MAX_ENDPOINTS_EXCEEDED      (0x20000000)
#define MPI3_PCIEIOUNIT0_ES_INSUFFICIENT_RESOURCES      (0x10000000)
#ifndef MPI3_PCIE_IO_UNIT0_PHY_MAX
#define MPI3_PCIE_IO_UNIT0_PHY_MAX      (1)
#endif
struct mpi3_pcie_io_unit_page0 {
	struct mpi3_config_page_header         header;
	__le32                             reserved08;
	u8                                 num_phys;
	u8                                 init_status;
	u8                                 aspm;
	u8                                 reserved0f;
	struct mpi3_pcie_io_unit0_phy_data     phy_data[MPI3_PCIE_IO_UNIT0_PHY_MAX];
};

#define MPI3_PCIEIOUNIT0_PAGEVERSION                        (0x00)
#define MPI3_PCIEIOUNIT0_INITSTATUS_NO_ERRORS               (0x00)
#define MPI3_PCIEIOUNIT0_INITSTATUS_NEEDS_INITIALIZATION    (0x01)
#define MPI3_PCIEIOUNIT0_INITSTATUS_NO_TARGETS_ALLOCATED    (0x02)
#define MPI3_PCIEIOUNIT0_INITSTATUS_RESOURCE_ALLOC_FAILED   (0x03)
#define MPI3_PCIEIOUNIT0_INITSTATUS_BAD_NUM_PHYS            (0x04)
#define MPI3_PCIEIOUNIT0_INITSTATUS_UNSUPPORTED_CONFIG      (0x05)
#define MPI3_PCIEIOUNIT0_INITSTATUS_HOST_PORT_MISMATCH      (0x06)
#define MPI3_PCIEIOUNIT0_INITSTATUS_PHYS_NOT_CONSECUTIVE    (0x07)
#define MPI3_PCIEIOUNIT0_INITSTATUS_BAD_CLOCKING_MODE       (0x08)
#define MPI3_PCIEIOUNIT0_INITSTATUS_PROD_SPEC_START         (0xf0)
#define MPI3_PCIEIOUNIT0_INITSTATUS_PROD_SPEC_END           (0xff)
#define MPI3_PCIEIOUNIT0_ASPM_SWITCH_STATES_MASK            (0xc0)
#define MPI3_PCIEIOUNIT0_ASPM_SWITCH_STATES_SHIFT              (6)
#define MPI3_PCIEIOUNIT0_ASPM_DIRECT_STATES_MASK            (0x30)
#define MPI3_PCIEIOUNIT0_ASPM_DIRECT_STATES_SHIFT              (4)
#define MPI3_PCIEIOUNIT0_ASPM_SWITCH_SUPPORT_MASK           (0x0c)
#define MPI3_PCIEIOUNIT0_ASPM_SWITCH_SUPPORT_SHIFT             (2)
#define MPI3_PCIEIOUNIT0_ASPM_DIRECT_SUPPORT_MASK           (0x03)
#define MPI3_PCIEIOUNIT0_ASPM_DIRECT_SUPPORT_SHIFT             (0)
struct mpi3_pcie_io_unit1_phy_data {
	u8         link;
	u8         link_flags;
	u8         phy_flags;
	u8         max_min_link_rate;
	__le32     reserved04;
	__le32     reserved08;
};

#define MPI3_PCIEIOUNIT1_LINKFLAGS_PCIE_CLK_MODE_MASK                     (0x03)
#define MPI3_PCIEIOUNIT1_LINKFLAGS_PCIE_CLK_MODE_DIS_SEPARATE_REFCLK      (0x00)
#define MPI3_PCIEIOUNIT1_LINKFLAGS_PCIE_CLK_MODE_EN_SRIS                  (0x01)
#define MPI3_PCIEIOUNIT1_LINKFLAGS_PCIE_CLK_MODE_EN_SRNS                  (0x02)
#define MPI3_PCIEIOUNIT1_PHYFLAGS_PHY_DISABLE                             (0x08)
#define MPI3_PCIEIOUNIT1_MMLR_MAX_RATE_MASK                               (0xf0)
#define MPI3_PCIEIOUNIT1_MMLR_MAX_RATE_SHIFT                                 (4)
#define MPI3_PCIEIOUNIT1_MMLR_MAX_RATE_2_5                                (0x20)
#define MPI3_PCIEIOUNIT1_MMLR_MAX_RATE_5_0                                (0x30)
#define MPI3_PCIEIOUNIT1_MMLR_MAX_RATE_8_0                                (0x40)
#define MPI3_PCIEIOUNIT1_MMLR_MAX_RATE_16_0                               (0x50)
#define MPI3_PCIEIOUNIT1_MMLR_MAX_RATE_32_0                               (0x60)
#ifndef MPI3_PCIE_IO_UNIT1_PHY_MAX
#define MPI3_PCIE_IO_UNIT1_PHY_MAX                                           (1)
#endif
struct mpi3_pcie_io_unit_page1 {
	struct mpi3_config_page_header         header;
	__le32                             control_flags;
	__le32                             reserved0c;
	u8                                 num_phys;
	u8                                 reserved11;
	u8                                 aspm;
	u8                                 reserved13;
	struct mpi3_pcie_io_unit1_phy_data     phy_data[MPI3_PCIE_IO_UNIT1_PHY_MAX];
};

#define MPI3_PCIEIOUNIT1_PAGEVERSION                                           (0x00)
#define MPI3_PCIEIOUNIT1_CONTROL_FLAGS_LINK_OVERRIDE_DISABLE                   (0x80)
#define MPI3_PCIEIOUNIT1_CONTROL_FLAGS_CLOCK_OVERRIDE_DISABLE                  (0x40)
#define MPI3_PCIEIOUNIT1_CONTROL_FLAGS_CLOCK_OVERRIDE_MODE_MASK                (0x30)
#define MPI3_PCIEIOUNIT1_CONTROL_FLAGS_CLOCK_OVERRIDE_MODE_SHIFT               (4)
#define MPI3_PCIEIOUNIT1_CONTROL_FLAGS_CLOCK_OVERRIDE_MODE_SRIS_SRNS_DISABLED  (0x00)
#define MPI3_PCIEIOUNIT1_CONTROL_FLAGS_CLOCK_OVERRIDE_MODE_SRIS_ENABLED        (0x10)
#define MPI3_PCIEIOUNIT1_CONTROL_FLAGS_CLOCK_OVERRIDE_MODE_SRNS_ENABLED        (0x20)
#define MPI3_PCIEIOUNIT1_CONTROL_FLAGS_LINK_RATE_OVERRIDE_MASK                 (0x0f)
#define MPI3_PCIEIOUNIT1_CONTROL_FLAGS_LINK_RATE_OVERRIDE_MAX_2_5              (0x02)
#define MPI3_PCIEIOUNIT1_CONTROL_FLAGS_LINK_RATE_OVERRIDE_MAX_5_0              (0x03)
#define MPI3_PCIEIOUNIT1_CONTROL_FLAGS_LINK_RATE_OVERRIDE_MAX_8_0              (0x04)
#define MPI3_PCIEIOUNIT1_CONTROL_FLAGS_LINK_RATE_OVERRIDE_MAX_16_0             (0x05)
#define MPI3_PCIEIOUNIT1_CONTROL_FLAGS_LINK_RATE_OVERRIDE_MAX_32_0             (0x06)
#define MPI3_PCIEIOUNIT1_ASPM_SWITCH_MASK                                 (0x0c)
#define MPI3_PCIEIOUNIT1_ASPM_SWITCH_SHIFT                                   (2)
#define MPI3_PCIEIOUNIT1_ASPM_DIRECT_MASK                                 (0x03)
#define MPI3_PCIEIOUNIT1_ASPM_DIRECT_SHIFT                                   (0)
struct mpi3_pcie_io_unit_page2 {
	struct mpi3_config_page_header         header;
	__le16                             nvme_max_q_dx1;
	__le16                             nvme_max_q_dx2;
	u8                                 nvme_abort_to;
	u8                                 reserved0d;
	__le16                             nvme_max_q_dx4;
};

#define MPI3_PCIEIOUNIT2_PAGEVERSION                        (0x00)
#define MPI3_PCIEIOUNIT3_ERROR_RECEIVER_ERROR               (0)
#define MPI3_PCIEIOUNIT3_ERROR_RECOVERY                     (1)
#define MPI3_PCIEIOUNIT3_ERROR_CORRECTABLE_ERROR_MSG        (2)
#define MPI3_PCIEIOUNIT3_ERROR_BAD_DLLP                     (3)
#define MPI3_PCIEIOUNIT3_ERROR_BAD_TLP                      (4)
#define MPI3_PCIEIOUNIT3_NUM_ERROR_INDEX                    (5)
struct mpi3_pcie_io_unit3_error {
	__le16                             threshold_count;
	__le16                             reserved02;
};

struct mpi3_pcie_io_unit_page3 {
	struct mpi3_config_page_header         header;
	u8                                 threshold_window;
	u8                                 threshold_action;
	u8                                 escalation_count;
	u8                                 escalation_action;
	u8                                 num_errors;
	u8                                 reserved0d[3];
	struct mpi3_pcie_io_unit3_error        error[MPI3_PCIEIOUNIT3_NUM_ERROR_INDEX];
};

#define MPI3_PCIEIOUNIT3_PAGEVERSION                        (0x00)
#define MPI3_PCIEIOUNIT3_ACTION_NO_ACTION                   (0x00)
#define MPI3_PCIEIOUNIT3_ACTION_HOT_RESET                   (0x01)
#define MPI3_PCIEIOUNIT3_ACTION_REDUCE_LINK_RATE_ONLY       (0x02)
#define MPI3_PCIEIOUNIT3_ACTION_REDUCE_LINK_RATE_NO_ACCESS  (0x03)
struct mpi3_pcie_switch_page0 {
	struct mpi3_config_page_header     header;
	u8                             io_unit_port;
	u8                             switch_status;
	u8                             reserved0a[2];
	__le16                         dev_handle;
	__le16                         parent_dev_handle;
	u8                             num_ports;
	u8                             pcie_level;
	__le16                         reserved12;
	__le32                         reserved14;
	__le32                         reserved18;
	__le32                         reserved1c;
};

#define MPI3_PCIESWITCH0_PAGEVERSION                  (0x00)
#define MPI3_PCIESWITCH0_SS_NOT_RESPONDING            (0x02)
#define MPI3_PCIESWITCH0_SS_RESPONDING                (0x03)
#define MPI3_PCIESWITCH0_SS_DELAY_NOT_RESPONDING      (0x04)
struct mpi3_pcie_switch_page1 {
	struct mpi3_config_page_header     header;
	u8                             io_unit_port;
	u8                             flags;
	__le16                         reserved0a;
	u8                             num_ports;
	u8                             port_num;
	__le16                         attached_dev_handle;
	__le16                         switch_dev_handle;
	u8                             negotiated_port_width;
	u8                             negotiated_link_rate;
	__le16                         slot;
	__le16                         slot_index;
	__le32                         reserved18;
};

#define MPI3_PCIESWITCH1_PAGEVERSION        (0x00)
#define MPI3_PCIESWITCH1_FLAGS_ASPMSTATE_MASK     (0x0c)
#define MPI3_PCIESWITCH1_FLAGS_ASPMSTATE_SHIFT    (2)
#define MPI3_PCIESWITCH1_FLAGS_ASPMSUPPORT_MASK     (0x03)
#define MPI3_PCIESWITCH1_FLAGS_ASPMSUPPORT_SHIFT    (0)
#ifndef MPI3_PCIESWITCH2_MAX_NUM_PORTS
#define MPI3_PCIESWITCH2_MAX_NUM_PORTS                               (1)
#endif
struct mpi3_pcieswitch2_port_element {
	__le16                             link_change_count;
	__le16                             rate_change_count;
	__le32                             reserved04;
};

struct mpi3_pcie_switch_page2 {
	struct mpi3_config_page_header         header;
	u8                                 num_ports;
	u8                                 reserved09;
	__le16                             dev_handle;
	__le32                             reserved0c;
	struct mpi3_pcieswitch2_port_element   port[MPI3_PCIESWITCH2_MAX_NUM_PORTS];
};

#define MPI3_PCIESWITCH2_PAGEVERSION        (0x00)
struct mpi3_pcie_link_page0 {
	struct mpi3_config_page_header     header;
	u8                             link;
	u8                             reserved09[3];
	__le32                         reserved0c;
	__le32                         receiver_error_count;
	__le32                         recovery_count;
	__le32                         corr_error_msg_count;
	__le32                         non_fatal_error_msg_count;
	__le32                         fatal_error_msg_count;
	__le32                         non_fatal_error_count;
	__le32                         fatal_error_count;
	__le32                         bad_dllp_count;
	__le32                         bad_tlp_count;
};

#define MPI3_PCIELINK0_PAGEVERSION          (0x00)
struct mpi3_enclosure_page0 {
	struct mpi3_config_page_header         header;
	__le64                             enclosure_logical_id;
	__le16                             flags;
	__le16                             enclosure_handle;
	__le16                             num_slots;
	__le16                             reserved16;
	u8                                 io_unit_port;
	u8                                 enclosure_level;
	__le16                             sep_dev_handle;
	u8                                 chassis_slot;
	u8                                 reserved1d[3];
};

#define MPI3_ENCLOSURE0_PAGEVERSION                     (0x00)
#define MPI3_ENCLS0_FLAGS_ENCL_TYPE_MASK                (0xc000)
#define MPI3_ENCLS0_FLAGS_ENCL_TYPE_VIRTUAL             (0x0000)
#define MPI3_ENCLS0_FLAGS_ENCL_TYPE_SAS                 (0x4000)
#define MPI3_ENCLS0_FLAGS_ENCL_TYPE_PCIE                (0x8000)
#define MPI3_ENCLS0_FLAGS_CHASSIS_SLOT_VALID            (0x0020)
#define MPI3_ENCLS0_FLAGS_ENCL_DEV_PRESENT_MASK         (0x0010)
#define MPI3_ENCLS0_FLAGS_ENCL_DEV_NOT_FOUND            (0x0000)
#define MPI3_ENCLS0_FLAGS_ENCL_DEV_PRESENT              (0x0010)
#define MPI3_ENCLS0_FLAGS_MNG_MASK                      (0x000f)
#define MPI3_ENCLS0_FLAGS_MNG_UNKNOWN                   (0x0000)
#define MPI3_ENCLS0_FLAGS_MNG_IOC_SES                   (0x0001)
#define MPI3_ENCLS0_FLAGS_MNG_SES_ENCLOSURE             (0x0002)
#define MPI3_DEVICE_DEVFORM_SAS_SATA                    (0x00)
#define MPI3_DEVICE_DEVFORM_PCIE                        (0x01)
#define MPI3_DEVICE_DEVFORM_VD                          (0x02)
struct mpi3_device0_sas_sata_format {
	__le64     sas_address;
	__le16     flags;
	__le16     device_info;
	u8         phy_num;
	u8         attached_phy_identifier;
	u8         max_port_connections;
	u8         zone_group;
};

#define MPI3_DEVICE0_SASSATA_FLAGS_WRITE_SAME_UNMAP_NCQ (0x0400)
#define MPI3_DEVICE0_SASSATA_FLAGS_SLUMBER_CAP          (0x0200)
#define MPI3_DEVICE0_SASSATA_FLAGS_PARTIAL_CAP          (0x0100)
#define MPI3_DEVICE0_SASSATA_FLAGS_ASYNC_NOTIFY         (0x0080)
#define MPI3_DEVICE0_SASSATA_FLAGS_SW_PRESERVE          (0x0040)
#define MPI3_DEVICE0_SASSATA_FLAGS_UNSUPP_DEV           (0x0020)
#define MPI3_DEVICE0_SASSATA_FLAGS_48BIT_LBA            (0x0010)
#define MPI3_DEVICE0_SASSATA_FLAGS_SMART_SUPP           (0x0008)
#define MPI3_DEVICE0_SASSATA_FLAGS_NCQ_SUPP             (0x0004)
#define MPI3_DEVICE0_SASSATA_FLAGS_FUA_SUPP             (0x0002)
#define MPI3_DEVICE0_SASSATA_FLAGS_PERSIST_CAP          (0x0001)
struct mpi3_device0_pcie_format {
	u8         supported_link_rates;
	u8         max_port_width;
	u8         negotiated_port_width;
	u8         negotiated_link_rate;
	u8         port_num;
	u8         controller_reset_to;
	__le16     device_info;
	__le32     maximum_data_transfer_size;
	__le32     capabilities;
	__le16     noiob;
	u8         nvme_abort_to;
	u8         page_size;
	__le16     shutdown_latency;
	u8         recovery_info;
	u8         reserved17;
};

#define MPI3_DEVICE0_PCIE_LINK_RATE_32_0_SUPP           (0x10)
#define MPI3_DEVICE0_PCIE_LINK_RATE_16_0_SUPP           (0x08)
#define MPI3_DEVICE0_PCIE_LINK_RATE_8_0_SUPP            (0x04)
#define MPI3_DEVICE0_PCIE_LINK_RATE_5_0_SUPP            (0x02)
#define MPI3_DEVICE0_PCIE_LINK_RATE_2_5_SUPP            (0x01)
#define MPI3_DEVICE0_PCIE_DEVICE_INFO_TYPE_MASK             (0x0007)
#define MPI3_DEVICE0_PCIE_DEVICE_INFO_TYPE_NO_DEVICE        (0x0000)
#define MPI3_DEVICE0_PCIE_DEVICE_INFO_TYPE_NVME_DEVICE      (0x0001)
#define MPI3_DEVICE0_PCIE_DEVICE_INFO_TYPE_SWITCH_DEVICE    (0x0002)
#define MPI3_DEVICE0_PCIE_DEVICE_INFO_TYPE_SCSI_DEVICE      (0x0003)
#define MPI3_DEVICE0_PCIE_DEVICE_INFO_ASPM_MASK             (0x0030)
#define MPI3_DEVICE0_PCIE_DEVICE_INFO_ASPM_SHIFT            (4)
#define MPI3_DEVICE0_PCIE_DEVICE_INFO_PITYPE_MASK           (0x00c0)
#define MPI3_DEVICE0_PCIE_DEVICE_INFO_PITYPE_SHIFT          (6)
#define MPI3_DEVICE0_PCIE_DEVICE_INFO_PITYPE_0              (0x0000)
#define MPI3_DEVICE0_PCIE_DEVICE_INFO_PITYPE_1              (0x0040)
#define MPI3_DEVICE0_PCIE_DEVICE_INFO_PITYPE_2              (0x0080)
#define MPI3_DEVICE0_PCIE_DEVICE_INFO_PITYPE_3              (0x00c0)
#define MPI3_DEVICE0_PCIE_CAP_SGL_EXTRA_LENGTH_SUPPORTED    (0x00000020)
#define MPI3_DEVICE0_PCIE_CAP_METADATA_SEPARATED            (0x00000010)
#define MPI3_DEVICE0_PCIE_CAP_SGL_DWORD_ALIGN_REQUIRED      (0x00000008)
#define MPI3_DEVICE0_PCIE_CAP_SGL_FORMAT_SGL                (0x00000004)
#define MPI3_DEVICE0_PCIE_CAP_SGL_FORMAT_PRP                (0x00000000)
#define MPI3_DEVICE0_PCIE_CAP_BIT_BUCKET_SGL_SUPP           (0x00000002)
#define MPI3_DEVICE0_PCIE_CAP_SGL_SUPP                      (0x00000001)
#define MPI3_DEVICE0_PCIE_CAP_ASPM_MASK                     (0x000000c0)
#define MPI3_DEVICE0_PCIE_CAP_ASPM_SHIFT                    (6)
#define MPI3_DEVICE0_PCIE_RECOVER_METHOD_MASK               (0xe0)
#define MPI3_DEVICE0_PCIE_RECOVER_METHOD_NS_MGMT            (0x00)
#define MPI3_DEVICE0_PCIE_RECOVER_METHOD_FORMAT             (0x20)
#define MPI3_DEVICE0_PCIE_RECOVER_REASON_MASK               (0x1f)
#define MPI3_DEVICE0_PCIE_RECOVER_REASON_NO_NS              (0x00)
#define MPI3_DEVICE0_PCIE_RECOVER_REASON_NO_NSID_1          (0x01)
#define MPI3_DEVICE0_PCIE_RECOVER_REASON_TOO_MANY_NS        (0x02)
#define MPI3_DEVICE0_PCIE_RECOVER_REASON_PROTECTION         (0x03)
#define MPI3_DEVICE0_PCIE_RECOVER_REASON_METADATA_SZ        (0x04)
#define MPI3_DEVICE0_PCIE_RECOVER_REASON_LBA_DATA_SZ        (0x05)
struct mpi3_device0_vd_format {
	u8         vd_state;
	u8         raid_level;
	__le16     device_info;
	__le16     flags;
	__le16     reserved06;
	__le32     reserved08[2];
};

#define MPI3_DEVICE0_VD_STATE_OFFLINE                       (0x00)
#define MPI3_DEVICE0_VD_STATE_PARTIALLY_DEGRADED            (0x01)
#define MPI3_DEVICE0_VD_STATE_DEGRADED                      (0x02)
#define MPI3_DEVICE0_VD_STATE_OPTIMAL                       (0x03)
#define MPI3_DEVICE0_VD_RAIDLEVEL_RAID_0                    (0)
#define MPI3_DEVICE0_VD_RAIDLEVEL_RAID_1                    (1)
#define MPI3_DEVICE0_VD_RAIDLEVEL_RAID_5                    (5)
#define MPI3_DEVICE0_VD_RAIDLEVEL_RAID_6                    (6)
#define MPI3_DEVICE0_VD_RAIDLEVEL_RAID_10                   (10)
#define MPI3_DEVICE0_VD_RAIDLEVEL_RAID_50                   (50)
#define MPI3_DEVICE0_VD_RAIDLEVEL_RAID_60                   (60)
#define MPI3_DEVICE0_VD_DEVICE_INFO_HDD                     (0x0010)
#define MPI3_DEVICE0_VD_DEVICE_INFO_SSD                     (0x0008)
#define MPI3_DEVICE0_VD_DEVICE_INFO_NVME                    (0x0004)
#define MPI3_DEVICE0_VD_DEVICE_INFO_SATA                    (0x0002)
#define MPI3_DEVICE0_VD_DEVICE_INFO_SAS                     (0x0001)
#define MPI3_DEVICE0_VD_FLAGS_METADATA_MODE_MASK            (0x0003)
#define MPI3_DEVICE0_VD_FLAGS_METADATA_MODE_NONE            (0x0000)
#define MPI3_DEVICE0_VD_FLAGS_METADATA_MODE_HOST            (0x0001)
#define MPI3_DEVICE0_VD_FLAGS_METADATA_MODE_IOC             (0x0002)
union mpi3_device0_dev_spec_format {
	struct mpi3_device0_sas_sata_format        sas_sata_format;
	struct mpi3_device0_pcie_format            pcie_format;
	struct mpi3_device0_vd_format              vd_format;
};

struct mpi3_device_page0 {
	struct mpi3_config_page_header         header;
	__le16                             dev_handle;
	__le16                             parent_dev_handle;
	__le16                             slot;
	__le16                             enclosure_handle;
	__le64                             wwid;
	__le16                             persistent_id;
	u8                                 io_unit_port;
	u8                                 access_status;
	__le16                             flags;
	__le16                             reserved1e;
	__le16                             slot_index;
	__le16                             queue_depth;
	u8                                 reserved24[3];
	u8                                 device_form;
	union mpi3_device0_dev_spec_format    device_specific;
};

#define MPI3_DEVICE0_PAGEVERSION                        (0x00)
#define MPI3_DEVICE0_PARENT_INVALID                     (0xffff)
#define MPI3_DEVICE0_ENCLOSURE_HANDLE_NO_ENCLOSURE      (0x0000)
#define MPI3_DEVICE0_WWID_INVALID                       (0xffffffffffffffff)
#define MPI3_DEVICE0_PERSISTENTID_INVALID               (0xffff)
#define MPI3_DEVICE0_IOUNITPORT_INVALID                 (0xff)
#define MPI3_DEVICE0_ASTATUS_NO_ERRORS                              (0x00)
#define MPI3_DEVICE0_ASTATUS_NEEDS_INITIALIZATION                   (0x01)
#define MPI3_DEVICE0_ASTATUS_CAP_UNSUPPORTED                        (0x02)
#define MPI3_DEVICE0_ASTATUS_DEVICE_BLOCKED                         (0x03)
#define MPI3_DEVICE0_ASTATUS_UNAUTHORIZED                           (0x04)
#define MPI3_DEVICE0_ASTATUS_DEVICE_MISSING_DELAY                   (0x05)
#define MPI3_DEVICE0_ASTATUS_PREPARE                                (0x06)
#define MPI3_DEVICE0_ASTATUS_SAFE_MODE                              (0x07)
#define MPI3_DEVICE0_ASTATUS_GENERIC_MAX                            (0x0f)
#define MPI3_DEVICE0_ASTATUS_SAS_UNKNOWN                            (0x10)
#define MPI3_DEVICE0_ASTATUS_ROUTE_NOT_ADDRESSABLE                  (0x11)
#define MPI3_DEVICE0_ASTATUS_SMP_ERROR_NOT_ADDRESSABLE              (0x12)
#define MPI3_DEVICE0_ASTATUS_SAS_MAX                                (0x1f)
#define MPI3_DEVICE0_ASTATUS_SIF_UNKNOWN                            (0x20)
#define MPI3_DEVICE0_ASTATUS_SIF_AFFILIATION_CONFLICT               (0x21)
#define MPI3_DEVICE0_ASTATUS_SIF_DIAG                               (0x22)
#define MPI3_DEVICE0_ASTATUS_SIF_IDENTIFICATION                     (0x23)
#define MPI3_DEVICE0_ASTATUS_SIF_CHECK_POWER                        (0x24)
#define MPI3_DEVICE0_ASTATUS_SIF_PIO_SN                             (0x25)
#define MPI3_DEVICE0_ASTATUS_SIF_MDMA_SN                            (0x26)
#define MPI3_DEVICE0_ASTATUS_SIF_UDMA_SN                            (0x27)
#define MPI3_DEVICE0_ASTATUS_SIF_ZONING_VIOLATION                   (0x28)
#define MPI3_DEVICE0_ASTATUS_SIF_NOT_ADDRESSABLE                    (0x29)
#define MPI3_DEVICE0_ASTATUS_SIF_MAX                                (0x2f)
#define MPI3_DEVICE0_ASTATUS_PCIE_UNKNOWN                           (0x30)
#define MPI3_DEVICE0_ASTATUS_PCIE_MEM_SPACE_ACCESS                  (0x31)
#define MPI3_DEVICE0_ASTATUS_PCIE_UNSUPPORTED                       (0x32)
#define MPI3_DEVICE0_ASTATUS_PCIE_MSIX_REQUIRED                     (0x33)
#define MPI3_DEVICE0_ASTATUS_PCIE_ECRC_REQUIRED                     (0x34)
#define MPI3_DEVICE0_ASTATUS_PCIE_MAX                               (0x3f)
#define MPI3_DEVICE0_ASTATUS_NVME_UNKNOWN                           (0x40)
#define MPI3_DEVICE0_ASTATUS_NVME_READY_TIMEOUT                     (0x41)
#define MPI3_DEVICE0_ASTATUS_NVME_DEVCFG_UNSUPPORTED                (0x42)
#define MPI3_DEVICE0_ASTATUS_NVME_IDENTIFY_FAILED                   (0x43)
#define MPI3_DEVICE0_ASTATUS_NVME_QCONFIG_FAILED                    (0x44)
#define MPI3_DEVICE0_ASTATUS_NVME_QCREATION_FAILED                  (0x45)
#define MPI3_DEVICE0_ASTATUS_NVME_EVENTCFG_FAILED                   (0x46)
#define MPI3_DEVICE0_ASTATUS_NVME_GET_FEATURE_STAT_FAILED           (0x47)
#define MPI3_DEVICE0_ASTATUS_NVME_IDLE_TIMEOUT                      (0x48)
#define MPI3_DEVICE0_ASTATUS_NVME_CTRL_FAILURE_STATUS               (0x49)
#define MPI3_DEVICE0_ASTATUS_NVME_INSUFFICIENT_POWER                (0x4a)
#define MPI3_DEVICE0_ASTATUS_NVME_DOORBELL_STRIDE                   (0x4b)
#define MPI3_DEVICE0_ASTATUS_NVME_MEM_PAGE_MIN_SIZE                 (0x4c)
#define MPI3_DEVICE0_ASTATUS_NVME_MEMORY_ALLOCATION                 (0x4d)
#define MPI3_DEVICE0_ASTATUS_NVME_COMPLETION_TIME                   (0x4e)
#define MPI3_DEVICE0_ASTATUS_NVME_BAR                               (0x4f)
#define MPI3_DEVICE0_ASTATUS_NVME_NS_DESCRIPTOR                     (0x50)
#define MPI3_DEVICE0_ASTATUS_NVME_INCOMPATIBLE_SETTINGS             (0x51)
#define MPI3_DEVICE0_ASTATUS_NVME_MAX                               (0x5f)
#define MPI3_DEVICE0_ASTATUS_VD_UNKNOWN                             (0x80)
#define MPI3_DEVICE0_ASTATUS_VD_MAX                                 (0x8f)
#define MPI3_DEVICE0_FLAGS_CONTROLLER_DEV_HANDLE        (0x0080)
#define MPI3_DEVICE0_FLAGS_HIDDEN                       (0x0008)
#define MPI3_DEVICE0_FLAGS_ATT_METHOD_MASK              (0x0006)
#define MPI3_DEVICE0_FLAGS_ATT_METHOD_NOT_DIR_ATTACHED  (0x0000)
#define MPI3_DEVICE0_FLAGS_ATT_METHOD_DIR_ATTACHED      (0x0002)
#define MPI3_DEVICE0_FLAGS_ATT_METHOD_VIRTUAL           (0x0004)
#define MPI3_DEVICE0_FLAGS_DEVICE_PRESENT               (0x0001)
#define MPI3_DEVICE0_QUEUE_DEPTH_NOT_APPLICABLE         (0x0000)
struct mpi3_device1_sas_sata_format {
	__le32                             reserved00;
};

struct mpi3_device1_pcie_format {
	__le16                             vendor_id;
	__le16                             device_id;
	__le16                             subsystem_vendor_id;
	__le16                             subsystem_id;
	__le32                             reserved08;
	u8                                 revision_id;
	u8                                 reserved0d;
	__le16                             pci_parameters;
};

#define MPI3_DEVICE1_PCIE_PARAMS_DATA_SIZE_128B              (0x0)
#define MPI3_DEVICE1_PCIE_PARAMS_DATA_SIZE_256B              (0x1)
#define MPI3_DEVICE1_PCIE_PARAMS_DATA_SIZE_512B              (0x2)
#define MPI3_DEVICE1_PCIE_PARAMS_DATA_SIZE_1024B             (0x3)
#define MPI3_DEVICE1_PCIE_PARAMS_DATA_SIZE_2048B             (0x4)
#define MPI3_DEVICE1_PCIE_PARAMS_DATA_SIZE_4096B             (0x5)
#define MPI3_DEVICE1_PCIE_PARAMS_MAX_READ_REQ_MASK           (0x01c0)
#define MPI3_DEVICE1_PCIE_PARAMS_MAX_READ_REQ_SHIFT          (6)
#define MPI3_DEVICE1_PCIE_PARAMS_CURR_MAX_PAYLOAD_MASK       (0x0038)
#define MPI3_DEVICE1_PCIE_PARAMS_CURR_MAX_PAYLOAD_SHIFT      (3)
#define MPI3_DEVICE1_PCIE_PARAMS_SUPP_MAX_PAYLOAD_MASK       (0x0007)
#define MPI3_DEVICE1_PCIE_PARAMS_SUPP_MAX_PAYLOAD_SHIFT      (0)
struct mpi3_device1_vd_format {
	__le32                             reserved00;
};

union mpi3_device1_dev_spec_format {
	struct mpi3_device1_sas_sata_format    sas_sata_format;
	struct mpi3_device1_pcie_format        pcie_format;
	struct mpi3_device1_vd_format          vd_format;
};

struct mpi3_device_page1 {
	struct mpi3_config_page_header         header;
	__le16                             dev_handle;
	__le16                             reserved0a;
	__le16                             link_change_count;
	__le16                             rate_change_count;
	__le16                             tm_count;
	__le16                             reserved12;
	__le32                             reserved14[10];
	u8                                 reserved3c[3];
	u8                                 device_form;
	union mpi3_device1_dev_spec_format    device_specific;
};

#define MPI3_DEVICE1_PAGEVERSION                            (0x00)
#define MPI3_DEVICE1_COUNTER_MAX                            (0xfffe)
#define MPI3_DEVICE1_COUNTER_INVALID                        (0xffff)
#endif
