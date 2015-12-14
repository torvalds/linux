/**
  * This header file contains global constant/enum definitions,
  * global variable declaration.
  */
#ifndef _LBS_DEB_DEFS_H_
#define _LBS_DEB_DEFS_H_

#ifndef DRV_NAME
#define DRV_NAME "libertas_tf"
#endif

#include <linux/spinlock.h>

#ifdef CONFIG_LIBERTAS_THINFIRM_DEBUG
#define DEBUG
#define PROC_DEBUG
#endif

#define LBTF_DEB_ENTER	0x00000001
#define LBTF_DEB_LEAVE	0x00000002
#define LBTF_DEB_MAIN	0x00000004
#define LBTF_DEB_NET	0x00000008
#define LBTF_DEB_MESH	0x00000010
#define LBTF_DEB_WEXT	0x00000020
#define LBTF_DEB_IOCTL	0x00000040
#define LBTF_DEB_SCAN	0x00000080
#define LBTF_DEB_ASSOC	0x00000100
#define LBTF_DEB_JOIN	0x00000200
#define LBTF_DEB_11D	0x00000400
#define LBTF_DEB_DEBUGFS	0x00000800
#define LBTF_DEB_ETHTOOL	0x00001000
#define LBTF_DEB_HOST	0x00002000
#define LBTF_DEB_CMD	0x00004000
#define LBTF_DEB_RX	0x00008000
#define LBTF_DEB_TX	0x00010000
#define LBTF_DEB_USB	0x00020000
#define LBTF_DEB_CS	0x00040000
#define LBTF_DEB_FW	0x00080000
#define LBTF_DEB_THREAD	0x00100000
#define LBTF_DEB_HEX	0x00200000
#define LBTF_DEB_SDIO	0x00400000
#define LBTF_DEB_MACOPS	0x00800000

extern unsigned int lbtf_debug;


#ifdef DEBUG
#define LBTF_DEB_LL(grp, grpnam, fmt, args...) \
do { if ((lbtf_debug & (grp)) == (grp)) \
  printk(KERN_DEBUG DRV_NAME grpnam "%s: " fmt, \
         in_interrupt() ? " (INT)" : "", ## args); } while (0)
#else
#define LBTF_DEB_LL(grp, grpnam, fmt, args...) do {} while (0)
#endif

#define lbtf_deb_enter(grp) \
  LBTF_DEB_LL(grp | LBTF_DEB_ENTER, " enter", "%s()\n", __func__);
#define lbtf_deb_enter_args(grp, fmt, args...) \
  LBTF_DEB_LL(grp | LBTF_DEB_ENTER, " enter", "%s(" fmt ")\n", __func__, ## args);
#define lbtf_deb_leave(grp) \
  LBTF_DEB_LL(grp | LBTF_DEB_LEAVE, " leave", "%s()\n", __func__);
#define lbtf_deb_leave_args(grp, fmt, args...) \
  LBTF_DEB_LL(grp | LBTF_DEB_LEAVE, " leave", "%s(), " fmt "\n", \
  __func__, ##args);
#define lbtf_deb_main(fmt, args...)      LBTF_DEB_LL(LBTF_DEB_MAIN, " main", fmt, ##args)
#define lbtf_deb_net(fmt, args...)       LBTF_DEB_LL(LBTF_DEB_NET, " net", fmt, ##args)
#define lbtf_deb_mesh(fmt, args...)      LBTF_DEB_LL(LBTF_DEB_MESH, " mesh", fmt, ##args)
#define lbtf_deb_wext(fmt, args...)      LBTF_DEB_LL(LBTF_DEB_WEXT, " wext", fmt, ##args)
#define lbtf_deb_ioctl(fmt, args...)     LBTF_DEB_LL(LBTF_DEB_IOCTL, " ioctl", fmt, ##args)
#define lbtf_deb_scan(fmt, args...)      LBTF_DEB_LL(LBTF_DEB_SCAN, " scan", fmt, ##args)
#define lbtf_deb_assoc(fmt, args...)     LBTF_DEB_LL(LBTF_DEB_ASSOC, " assoc", fmt, ##args)
#define lbtf_deb_join(fmt, args...)      LBTF_DEB_LL(LBTF_DEB_JOIN, " join", fmt, ##args)
#define lbtf_deb_11d(fmt, args...)       LBTF_DEB_LL(LBTF_DEB_11D, " 11d", fmt, ##args)
#define lbtf_deb_debugfs(fmt, args...)   LBTF_DEB_LL(LBTF_DEB_DEBUGFS, " debugfs", fmt, ##args)
#define lbtf_deb_ethtool(fmt, args...)   LBTF_DEB_LL(LBTF_DEB_ETHTOOL, " ethtool", fmt, ##args)
#define lbtf_deb_host(fmt, args...)      LBTF_DEB_LL(LBTF_DEB_HOST, " host", fmt, ##args)
#define lbtf_deb_cmd(fmt, args...)       LBTF_DEB_LL(LBTF_DEB_CMD, " cmd", fmt, ##args)
#define lbtf_deb_rx(fmt, args...)        LBTF_DEB_LL(LBTF_DEB_RX, " rx", fmt, ##args)
#define lbtf_deb_tx(fmt, args...)        LBTF_DEB_LL(LBTF_DEB_TX, " tx", fmt, ##args)
#define lbtf_deb_fw(fmt, args...)        LBTF_DEB_LL(LBTF_DEB_FW, " fw", fmt, ##args)
#define lbtf_deb_usb(fmt, args...)       LBTF_DEB_LL(LBTF_DEB_USB, " usb", fmt, ##args)
#define lbtf_deb_usbd(dev, fmt, args...) LBTF_DEB_LL(LBTF_DEB_USB, " usbd", "%s:" fmt, dev_name(dev), ##args)
#define lbtf_deb_cs(fmt, args...)        LBTF_DEB_LL(LBTF_DEB_CS, " cs", fmt, ##args)
#define lbtf_deb_thread(fmt, args...)    LBTF_DEB_LL(LBTF_DEB_THREAD, " thread", fmt, ##args)
#define lbtf_deb_sdio(fmt, args...)      LBTF_DEB_LL(LBTF_DEB_SDIO, " thread", fmt, ##args)
#define lbtf_deb_macops(fmt, args...)      LBTF_DEB_LL(LBTF_DEB_MACOPS, " thread", fmt, ##args)

#ifdef DEBUG
static inline void lbtf_deb_hex(unsigned int grp, const char *prompt, u8 *buf, int len)
{
	char newprompt[32];

	if (len &&
	    (lbtf_debug & LBTF_DEB_HEX) &&
	    (lbtf_debug & grp))	{
		snprintf(newprompt, sizeof(newprompt), DRV_NAME " %s: ", prompt);
		print_hex_dump_bytes(prompt, DUMP_PREFIX_NONE, buf, len);
	}
}
#else
#define lbtf_deb_hex(grp, prompt, buf, len)	do {} while (0)
#endif

#endif
