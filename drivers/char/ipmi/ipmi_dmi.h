/*
 * DMI defines for use by IPMI
 */

#define IPMI_DMI_TYPE_KCS	0x01
#define IPMI_DMI_TYPE_SMIC	0x02
#define IPMI_DMI_TYPE_BT	0x03
#define IPMI_DMI_TYPE_SSIF	0x04

#ifdef CONFIG_IPMI_DMI_DECODE
int ipmi_dmi_get_slave_addr(int type, u32 flags, unsigned long base_addr);
#endif
