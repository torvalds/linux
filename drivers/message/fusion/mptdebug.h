/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  linux/drivers/message/fusion/mptde.h
 *      For use with LSI PCI chip/adapter(s)
 *      running LSI Fusion MPT (Message Passing Technology) firmware.
 *
 *  Copyright (c) 1999-2008 LSI Corporation
 *  (mailto:DL-MPTFusionLinux@lsi.com)
 *
 */
/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/

#ifndef MPTDE_H_INCLUDED
#define MPTDE_H_INCLUDED

/*
 * de level can be programmed on the fly via SysFS (hex values)
 *
 * Example:  (programming for MPT_DE_EVENTS on host 5)
 *
 * echo 8 > /sys/class/scsi_host/host5/de_level
 *
 * --------------------------------------------------------
 * mpt_de_level - command line parameter
 * this allow enabling de at driver load time (for all iocs)
 *
 * Example  (programming for MPT_DE_EVENTS)
 *
 * insmod mptbase.ko mpt_de_level=8
 *
 * --------------------------------------------------------
 * CONFIG_FUSION_LOGGING - enables compiling de into driver
 * this can be enabled in the driver Makefile
 *
 *
 * --------------------------------------------------------
 * Please note most de prints are set to logging priority = de
 * This is the lowest level, and most verbose.  Please refer to manual
 * pages for syslogd or syslogd-ng on how to configure this.
 */

#define MPT_DE			0x00000001
#define MPT_DE_MSG_FRAME		0x00000002
#define MPT_DE_SG			0x00000004
#define MPT_DE_EVENTS		0x00000008
#define MPT_DE_VERBOSE_EVENTS	0x00000010
#define MPT_DE_INIT			0x00000020
#define MPT_DE_EXIT			0x00000040
#define MPT_DE_FAIL			0x00000080
#define MPT_DE_TM			0x00000100
#define MPT_DE_DV			0x00000200
#define MPT_DE_REPLY			0x00000400
#define MPT_DE_HANDSHAKE		0x00000800
#define MPT_DE_CONFIG		0x00001000
#define MPT_DE_DL			0x00002000
#define MPT_DE_RESET			0x00008000
#define MPT_DE_SCSI			0x00010000
#define MPT_DE_IOCTL			0x00020000
#define MPT_DE_FC			0x00080000
#define MPT_DE_SAS			0x00100000
#define MPT_DE_SAS_WIDE		0x00200000
#define MPT_DE_36GB_MEM              0x00400000

/*
 * CONFIG_FUSION_LOGGING - enabled in Kconfig
 */

#ifdef CONFIG_FUSION_LOGGING
#define MPT_CHECK_LOGGING(IOC, CMD, BITS)			\
{								\
	if (IOC->de_level & BITS)				\
		CMD;						\
}
#else
#define MPT_CHECK_LOGGING(IOC, CMD, BITS)
#endif


/*
 * de macros
 */

#define dprintk(IOC, CMD)			\
	MPT_CHECK_LOGGING(IOC, CMD, MPT_DE)

#define dsgprintk(IOC, CMD)			\
	MPT_CHECK_LOGGING(IOC, CMD, MPT_DE_SG)

#define devtprintk(IOC, CMD)			\
	MPT_CHECK_LOGGING(IOC, CMD, MPT_DE_EVENTS)

#define devtverboseprintk(IOC, CMD)		\
	MPT_CHECK_LOGGING(IOC, CMD, MPT_DE_VERBOSE_EVENTS)

#define dinitprintk(IOC, CMD)			\
	MPT_CHECK_LOGGING(IOC, CMD, MPT_DE_INIT)

#define dexitprintk(IOC, CMD)			\
	MPT_CHECK_LOGGING(IOC, CMD, MPT_DE_EXIT)

#define dfailprintk(IOC, CMD)			\
	MPT_CHECK_LOGGING(IOC, CMD, MPT_DE_FAIL)

#define dtmprintk(IOC, CMD)			\
	MPT_CHECK_LOGGING(IOC, CMD, MPT_DE_TM)

#define ddvprintk(IOC, CMD)			\
	MPT_CHECK_LOGGING(IOC, CMD, MPT_DE_DV)

#define dreplyprintk(IOC, CMD)			\
	MPT_CHECK_LOGGING(IOC, CMD, MPT_DE_REPLY)

#define dhsprintk(IOC, CMD)			\
	MPT_CHECK_LOGGING(IOC, CMD, MPT_DE_HANDSHAKE)

#define dcprintk(IOC, CMD)			\
	MPT_CHECK_LOGGING(IOC, CMD, MPT_DE_CONFIG)

#define ddlprintk(IOC, CMD)			\
	MPT_CHECK_LOGGING(IOC, CMD, MPT_DE_DL)

#define drsprintk(IOC, CMD)			\
	MPT_CHECK_LOGGING(IOC, CMD, MPT_DE_RESET)

#define dsprintk(IOC, CMD)			\
	MPT_CHECK_LOGGING(IOC, CMD, MPT_DE_SCSI)

#define dctlprintk(IOC, CMD)			\
	MPT_CHECK_LOGGING(IOC, CMD, MPT_DE_IOCTL)

#define dfcprintk(IOC, CMD)			\
	MPT_CHECK_LOGGING(IOC, CMD, MPT_DE_FC)

#define dsasprintk(IOC, CMD)			\
	MPT_CHECK_LOGGING(IOC, CMD, MPT_DE_SAS)

#define dsaswideprintk(IOC, CMD)		\
	MPT_CHECK_LOGGING(IOC, CMD, MPT_DE_SAS_WIDE)

#define d36memprintk(IOC, CMD)		\
	MPT_CHECK_LOGGING(IOC, CMD, MPT_DE_36GB_MEM)


/*
 * Verbose logging
 */
#if defined(MPT_DE_VERBOSE) && defined(CONFIG_FUSION_LOGGING)
static inline void
DBG_DUMP_FW_DOWNLOAD(MPT_ADAPTER *ioc, u32  *mfp, int numfrags)
{
	int i;

	if (!(ioc->de_level & MPT_DE))
		return;
	printk(KERN_DE "F/W download request:\n");
	for (i=0; i < 7+numfrags*2; i++)
		printk(" %08x", le32_to_cpu(mfp[i]));
	printk("\n");
}

static inline void
DBG_DUMP_PUT_MSG_FRAME(MPT_ADAPTER *ioc, u32 *mfp)
{
	int	 ii, n;

	if (!(ioc->de_level & MPT_DE_MSG_FRAME))
		return;
	printk(KERN_DE "%s: About to Put msg frame @ %p:\n",
		ioc->name, mfp);
	n = ioc->req_sz/4 - 1;
	while (mfp[n] == 0)
		n--;
	for (ii=0; ii<=n; ii++) {
		if (ii && ((ii%8)==0))
			printk("\n");
		printk(" %08x", le32_to_cpu(mfp[ii]));
	}
	printk("\n");
}

static inline void
DBG_DUMP_FW_REQUEST_FRAME(MPT_ADAPTER *ioc, u32 *mfp)
{
	int  i, n;

	if (!(ioc->de_level & MPT_DE_MSG_FRAME))
		return;
	n = 10;
	printk(KERN_INFO " ");
	for (i = 0; i < n; i++)
		printk(" %08x", le32_to_cpu(mfp[i]));
	printk("\n");
}

static inline void
DBG_DUMP_REQUEST_FRAME(MPT_ADAPTER *ioc, u32 *mfp)
{
	int  i, n;

	if (!(ioc->de_level & MPT_DE_MSG_FRAME))
		return;
	n = 24;
	for (i=0; i<n; i++) {
		if (i && ((i%8)==0))
			printk("\n");
		printk("%08x ", le32_to_cpu(mfp[i]));
	}
	printk("\n");
}

static inline void
DBG_DUMP_REPLY_FRAME(MPT_ADAPTER *ioc, u32 *mfp)
{
	int  i, n;

	if (!(ioc->de_level & MPT_DE_MSG_FRAME))
		return;
	n = (le32_to_cpu(mfp[0]) & 0x00FF0000) >> 16;
	printk(KERN_INFO " ");
	for (i=0; i<n; i++)
		printk(" %08x", le32_to_cpu(mfp[i]));
	printk("\n");
}

static inline void
DBG_DUMP_REQUEST_FRAME_HDR(MPT_ADAPTER *ioc, u32 *mfp)
{
	int  i, n;

	if (!(ioc->de_level & MPT_DE_MSG_FRAME))
		return;
	n = 3;
	printk(KERN_INFO " ");
	for (i=0; i<n; i++)
		printk(" %08x", le32_to_cpu(mfp[i]));
	printk("\n");
}

static inline void
DBG_DUMP_TM_REQUEST_FRAME(MPT_ADAPTER *ioc, u32 *mfp)
{
	int  i, n;

	if (!(ioc->de_level & MPT_DE_TM))
		return;
	n = 13;
	printk(KERN_DE "TM_REQUEST:\n");
	for (i=0; i<n; i++) {
		if (i && ((i%8)==0))
			printk("\n");
		printk("%08x ", le32_to_cpu(mfp[i]));
	}
	printk("\n");
}

static inline void
DBG_DUMP_TM_REPLY_FRAME(MPT_ADAPTER *ioc, u32 *mfp)
{
	int  i, n;

	if (!(ioc->de_level & MPT_DE_TM))
		return;
	n = (le32_to_cpu(mfp[0]) & 0x00FF0000) >> 16;
	printk(KERN_DE "TM_REPLY MessageLength=%d:\n", n);
	for (i=0; i<n; i++) {
		if (i && ((i%8)==0))
			printk("\n");
		printk(" %08x", le32_to_cpu(mfp[i]));
	}
	printk("\n");
}

#define dmfprintk(IOC, CMD)			\
	MPT_CHECK_LOGGING(IOC, CMD, MPT_DE_MSG_FRAME)

# else /* ifdef MPT_DE_MF */

#define DBG_DUMP_FW_DOWNLOAD(IOC, mfp, numfrags)
#define DBG_DUMP_PUT_MSG_FRAME(IOC, mfp)
#define DBG_DUMP_FW_REQUEST_FRAME(IOC, mfp)
#define DBG_DUMP_REQUEST_FRAME(IOC, mfp)
#define DBG_DUMP_REPLY_FRAME(IOC, mfp)
#define DBG_DUMP_REQUEST_FRAME_HDR(IOC, mfp)
#define DBG_DUMP_TM_REQUEST_FRAME(IOC, mfp)
#define DBG_DUMP_TM_REPLY_FRAME(IOC, mfp)

#define dmfprintk(IOC, CMD)			\
	MPT_CHECK_LOGGING(IOC, CMD, MPT_DE_MSG_FRAME)

#endif /* defined(MPT_DE_VERBOSE) && defined(CONFIG_FUSION_LOGGING) */

#endif /* ifndef MPTDE_H_INCLUDED */
