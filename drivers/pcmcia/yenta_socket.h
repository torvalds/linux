#ifndef __YENTA_H
#define __YENTA_H

#include <asm/io.h>

#define CB_SOCKET_EVENT		0x00
#define    CB_CSTSEVENT		0x00000001	/* Card status event */
#define    CB_CD1EVENT		0x00000002	/* Card detect 1 change event */
#define    CB_CD2EVENT		0x00000004	/* Card detect 2 change event */
#define    CB_PWREVENT		0x00000008	/* PWRCYCLE change event */

#define CB_SOCKET_MASK		0x04
#define    CB_CSTSMASK		0x00000001	/* Card status mask */
#define    CB_CDMASK		0x00000006	/* Card detect 1&2 mask */
#define    CB_PWRMASK		0x00000008	/* PWRCYCLE change mask */

#define CB_SOCKET_STATE		0x08
#define    CB_CARDSTS		0x00000001	/* CSTSCHG status */
#define    CB_CDETECT1		0x00000002	/* Card detect status 1 */
#define    CB_CDETECT2		0x00000004	/* Card detect status 2 */
#define    CB_PWRCYCLE		0x00000008	/* Socket powered */
#define    CB_16BITCARD		0x00000010	/* 16-bit card detected */
#define    CB_CBCARD		0x00000020	/* CardBus card detected */
#define    CB_IREQCINT		0x00000040	/* READY(xIRQ)/xCINT high */
#define    CB_NOTACARD		0x00000080	/* Unrecognizable PC card detected */
#define    CB_DATALOST		0x00000100	/* Potential data loss due to card removal */
#define    CB_BADVCCREQ		0x00000200	/* Invalid Vcc request by host software */
#define    CB_5VCARD		0x00000400	/* Card Vcc at 5.0 volts? */
#define    CB_3VCARD		0x00000800	/* Card Vcc at 3.3 volts? */
#define    CB_XVCARD		0x00001000	/* Card Vcc at X.X volts? */
#define    CB_YVCARD		0x00002000	/* Card Vcc at Y.Y volts? */
#define    CB_5VSOCKET		0x10000000	/* Socket Vcc at 5.0 volts? */
#define    CB_3VSOCKET		0x20000000	/* Socket Vcc at 3.3 volts? */
#define    CB_XVSOCKET		0x40000000	/* Socket Vcc at X.X volts? */
#define    CB_YVSOCKET		0x80000000	/* Socket Vcc at Y.Y volts? */

#define CB_SOCKET_FORCE		0x0C
#define    CB_FCARDSTS		0x00000001	/* Force CSTSCHG */
#define    CB_FCDETECT1		0x00000002	/* Force CD1EVENT */
#define    CB_FCDETECT2		0x00000004	/* Force CD2EVENT */
#define    CB_FPWRCYCLE		0x00000008	/* Force PWREVENT */
#define    CB_F16BITCARD	0x00000010	/* Force 16-bit PCMCIA card */
#define    CB_FCBCARD		0x00000020	/* Force CardBus line */
#define    CB_FNOTACARD		0x00000080	/* Force NOTACARD */
#define    CB_FDATALOST		0x00000100	/* Force data lost */
#define    CB_FBADVCCREQ	0x00000200	/* Force bad Vcc request */
#define    CB_F5VCARD		0x00000400	/* Force 5.0 volt card */
#define    CB_F3VCARD		0x00000800	/* Force 3.3 volt card */
#define    CB_FXVCARD		0x00001000	/* Force X.X volt card */
#define    CB_FYVCARD		0x00002000	/* Force Y.Y volt card */
#define    CB_CVSTEST		0x00004000	/* Card VS test */

#define CB_SOCKET_CONTROL	0x10
#define  CB_SC_VPP_MASK		0x00000007
#define   CB_SC_VPP_OFF		0x00000000
#define   CB_SC_VPP_12V		0x00000001
#define   CB_SC_VPP_5V		0x00000002
#define   CB_SC_VPP_3V		0x00000003
#define   CB_SC_VPP_XV		0x00000004
#define   CB_SC_VPP_YV		0x00000005
#define  CB_SC_VCC_MASK		0x00000070
#define   CB_SC_VCC_OFF		0x00000000
#define   CB_SC_VCC_5V		0x00000020
#define   CB_SC_VCC_3V		0x00000030
#define   CB_SC_VCC_XV		0x00000040
#define   CB_SC_VCC_YV		0x00000050
#define  CB_SC_CCLK_STOP	0x00000080

#define CB_SOCKET_POWER		0x20
#define    CB_SKTACCES		0x02000000	/* A PC card access has occurred (clear on read) */
#define    CB_SKTMODE		0x01000000	/* Clock frequency has changed (clear on read) */
#define    CB_CLKCTRLEN		0x00010000	/* Clock control enabled (RW) */
#define    CB_CLKCTRL		0x00000001	/* Stop(0) or slow(1) CB clock (RW) */

/*
 * Cardbus configuration space
 */
#define CB_BRIDGE_BASE(m)	(0x1c + 8*(m))
#define CB_BRIDGE_LIMIT(m)	(0x20 + 8*(m))
#define CB_BRIDGE_CONTROL	0x3e
#define   CB_BRIDGE_CPERREN	0x00000001
#define   CB_BRIDGE_CSERREN	0x00000002
#define   CB_BRIDGE_ISAEN	0x00000004
#define   CB_BRIDGE_VGAEN	0x00000008
#define   CB_BRIDGE_MABTMODE	0x00000020
#define   CB_BRIDGE_CRST	0x00000040
#define   CB_BRIDGE_INTR	0x00000080
#define   CB_BRIDGE_PREFETCH0	0x00000100
#define   CB_BRIDGE_PREFETCH1	0x00000200
#define   CB_BRIDGE_POSTEN	0x00000400
#define CB_LEGACY_MODE_BASE	0x44

/*
 * ExCA area extensions in Yenta
 */
#define CB_MEM_PAGE(map)	(0x40 + (map))


/* control how 16bit cards are powered */
#define YENTA_16BIT_POWER_EXCA	0x00000001
#define YENTA_16BIT_POWER_DF	0x00000002


struct yenta_socket;

struct cardbus_type {
	int	(*override)(struct yenta_socket *);
	void	(*save_state)(struct yenta_socket *);
	void	(*restore_state)(struct yenta_socket *);
	int	(*sock_init)(struct yenta_socket *);
};

struct yenta_socket {
	struct pci_dev *dev;
	int cb_irq, io_irq;
	void __iomem *base;
	struct timer_list poll_timer;

	struct pcmcia_socket socket;
	struct cardbus_type *type;

	u32 flags;

	/* for PCI interrupt probing */
	unsigned int probe_status;

	/* A few words of private data for special stuff of overrides... */
	unsigned int private[8];

	/* PCI saved state */
	u32 saved_state[2];
};


#endif
