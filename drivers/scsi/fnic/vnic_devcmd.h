/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2008 Cisco Systems, Inc.  All rights reserved.
 * Copyright 2007 Nuova Systems, Inc.  All rights reserved.
 */
#ifndef _VNIC_DEVCMD_H_
#define _VNIC_DEVCMD_H_

#define _CMD_NBITS      14
#define _CMD_VTYPEBITS	10
#define _CMD_FLAGSBITS  6
#define _CMD_DIRBITS	2

#define _CMD_NMASK      ((1 << _CMD_NBITS)-1)
#define _CMD_VTYPEMASK  ((1 << _CMD_VTYPEBITS)-1)
#define _CMD_FLAGSMASK  ((1 << _CMD_FLAGSBITS)-1)
#define _CMD_DIRMASK    ((1 << _CMD_DIRBITS)-1)

#define _CMD_NSHIFT     0
#define _CMD_VTYPESHIFT (_CMD_NSHIFT+_CMD_NBITS)
#define _CMD_FLAGSSHIFT (_CMD_VTYPESHIFT+_CMD_VTYPEBITS)
#define _CMD_DIRSHIFT   (_CMD_FLAGSSHIFT+_CMD_FLAGSBITS)

/*
 * Direction bits (from host perspective).
 */
#define _CMD_DIR_NONE   0U
#define _CMD_DIR_WRITE  1U
#define _CMD_DIR_READ   2U
#define _CMD_DIR_RW     (_CMD_DIR_WRITE | _CMD_DIR_READ)

/*
 * Flag bits.
 */
#define _CMD_FLAGS_NONE 0U
#define _CMD_FLAGS_NOWAIT 1U

/*
 * vNIC type bits.
 */
#define _CMD_VTYPE_NONE  0U
#define _CMD_VTYPE_ENET  1U
#define _CMD_VTYPE_FC    2U
#define _CMD_VTYPE_SCSI  4U
#define _CMD_VTYPE_ALL   (_CMD_VTYPE_ENET | _CMD_VTYPE_FC | _CMD_VTYPE_SCSI)

/*
 * Used to create cmds..
*/
#define _CMDCF(dir, flags, vtype, nr)  \
	(((dir)   << _CMD_DIRSHIFT) | \
	((flags) << _CMD_FLAGSSHIFT) | \
	((vtype) << _CMD_VTYPESHIFT) | \
	((nr)    << _CMD_NSHIFT))
#define _CMDC(dir, vtype, nr)    _CMDCF(dir, 0, vtype, nr)
#define _CMDCNW(dir, vtype, nr)  _CMDCF(dir, _CMD_FLAGS_NOWAIT, vtype, nr)

/*
 * Used to decode cmds..
*/
#define _CMD_DIR(cmd)            (((cmd) >> _CMD_DIRSHIFT) & _CMD_DIRMASK)
#define _CMD_FLAGS(cmd)          (((cmd) >> _CMD_FLAGSSHIFT) & _CMD_FLAGSMASK)
#define _CMD_VTYPE(cmd)          (((cmd) >> _CMD_VTYPESHIFT) & _CMD_VTYPEMASK)
#define _CMD_N(cmd)              (((cmd) >> _CMD_NSHIFT) & _CMD_NMASK)

enum vnic_devcmd_cmd {
	CMD_NONE                = _CMDC(_CMD_DIR_NONE, _CMD_VTYPE_NONE, 0),

	/* mcpu fw info in mem: (u64)a0=paddr to struct vnic_devcmd_fw_info */
	CMD_MCPU_FW_INFO        = _CMDC(_CMD_DIR_WRITE, _CMD_VTYPE_ALL, 1),

	/* dev-specific block member:
	 *    in: (u16)a0=offset,(u8)a1=size
	 *    out: a0=value */
	CMD_DEV_SPEC            = _CMDC(_CMD_DIR_RW, _CMD_VTYPE_ALL, 2),

	/* stats clear */
	CMD_STATS_CLEAR         = _CMDCNW(_CMD_DIR_NONE, _CMD_VTYPE_ALL, 3),

	/* stats dump in mem: (u64)a0=paddr to stats area,
	 *                    (u16)a1=sizeof stats area */
	CMD_STATS_DUMP          = _CMDC(_CMD_DIR_WRITE, _CMD_VTYPE_ALL, 4),

	/* set Rx packet filter: (u32)a0=filters (see CMD_PFILTER_*) */
	CMD_PACKET_FILTER       = _CMDCNW(_CMD_DIR_WRITE, _CMD_VTYPE_ALL, 7),

	/* hang detection notification */
	CMD_HANG_NOTIFY         = _CMDC(_CMD_DIR_NONE, _CMD_VTYPE_ALL, 8),

	/* MAC address in (u48)a0 */
	CMD_MAC_ADDR            = _CMDC(_CMD_DIR_READ,
					_CMD_VTYPE_ENET | _CMD_VTYPE_FC, 9),

	/* disable/enable promisc mode: (u8)a0=0/1 */
/***** XXX DEPRECATED *****/
	CMD_PROMISC_MODE        = _CMDCNW(_CMD_DIR_WRITE, _CMD_VTYPE_ENET, 10),

	/* disable/enable all-multi mode: (u8)a0=0/1 */
/***** XXX DEPRECATED *****/
	CMD_ALLMULTI_MODE       = _CMDCNW(_CMD_DIR_WRITE, _CMD_VTYPE_ENET, 11),

	/* add addr from (u48)a0 */
	CMD_ADDR_ADD            = _CMDCNW(_CMD_DIR_WRITE,
					_CMD_VTYPE_ENET | _CMD_VTYPE_FC, 12),

	/* del addr from (u48)a0 */
	CMD_ADDR_DEL            = _CMDCNW(_CMD_DIR_WRITE,
					_CMD_VTYPE_ENET | _CMD_VTYPE_FC, 13),

	/* add VLAN id in (u16)a0 */
	CMD_VLAN_ADD            = _CMDCNW(_CMD_DIR_WRITE, _CMD_VTYPE_ENET, 14),

	/* del VLAN id in (u16)a0 */
	CMD_VLAN_DEL            = _CMDCNW(_CMD_DIR_WRITE, _CMD_VTYPE_ENET, 15),

	/* nic_cfg in (u32)a0 */
	CMD_NIC_CFG             = _CMDCNW(_CMD_DIR_WRITE, _CMD_VTYPE_ALL, 16),

	/* union vnic_rss_key in mem: (u64)a0=paddr, (u16)a1=len */
	CMD_RSS_KEY             = _CMDC(_CMD_DIR_WRITE, _CMD_VTYPE_ENET, 17),

	/* union vnic_rss_cpu in mem: (u64)a0=paddr, (u16)a1=len */
	CMD_RSS_CPU             = _CMDC(_CMD_DIR_WRITE, _CMD_VTYPE_ENET, 18),

	/* initiate softreset */
	CMD_SOFT_RESET          = _CMDCNW(_CMD_DIR_NONE, _CMD_VTYPE_ALL, 19),

	/* softreset status:
	 *    out: a0=0 reset complete, a0=1 reset in progress */
	CMD_SOFT_RESET_STATUS   = _CMDC(_CMD_DIR_READ, _CMD_VTYPE_ALL, 20),

	/* set struct vnic_devcmd_notify buffer in mem:
	 * in:
	 *   (u64)a0=paddr to notify (set paddr=0 to unset)
	 *   (u32)a1 & 0x00000000ffffffff=sizeof(struct vnic_devcmd_notify)
	 *   (u16)a1 & 0x0000ffff00000000=intr num (-1 for no intr)
	 * out:
	 *   (u32)a1 = effective size
	 */
	CMD_NOTIFY              = _CMDC(_CMD_DIR_RW, _CMD_VTYPE_ALL, 21),

	/* UNDI API: (u64)a0=paddr to s_PXENV_UNDI_ struct,
	 *           (u8)a1=PXENV_UNDI_xxx */
	CMD_UNDI                = _CMDC(_CMD_DIR_WRITE, _CMD_VTYPE_ENET, 22),

	/* initiate open sequence (u32)a0=flags (see CMD_OPENF_*) */
	CMD_OPEN		= _CMDCNW(_CMD_DIR_WRITE, _CMD_VTYPE_ALL, 23),

	/* open status:
	 *    out: a0=0 open complete, a0=1 open in progress */
	CMD_OPEN_STATUS		= _CMDC(_CMD_DIR_READ, _CMD_VTYPE_ALL, 24),

	/* close vnic */
	CMD_CLOSE		= _CMDC(_CMD_DIR_NONE, _CMD_VTYPE_ALL, 25),

	/* initialize virtual link: (u32)a0=flags (see CMD_INITF_*) */
	CMD_INIT		= _CMDCNW(_CMD_DIR_READ, _CMD_VTYPE_ALL, 26),

	/* variant of CMD_INIT, with provisioning info
	 *     (u64)a0=paddr of vnic_devcmd_provinfo
	 *     (u32)a1=sizeof provision info
	 */
	CMD_INIT_PROV_INFO	= _CMDC(_CMD_DIR_WRITE, _CMD_VTYPE_ENET, 27),

	/* enable virtual link */
	CMD_ENABLE		= _CMDCNW(_CMD_DIR_WRITE, _CMD_VTYPE_ALL, 28),

	/* disable virtual link */
	CMD_DISABLE		= _CMDC(_CMD_DIR_NONE, _CMD_VTYPE_ALL, 29),

	/* stats dump all vnics on uplink in mem: (u64)a0=paddr (u32)a1=uif */
	CMD_STATS_DUMP_ALL	= _CMDC(_CMD_DIR_WRITE, _CMD_VTYPE_ALL, 30),

	/* init status:
	 *    out: a0=0 init complete, a0=1 init in progress
	 *         if a0=0, a1=errno */
	CMD_INIT_STATUS		= _CMDC(_CMD_DIR_READ, _CMD_VTYPE_ALL, 31),

	/* INT13 API: (u64)a0=paddr to vnic_int13_params struct
	 *            (u8)a1=INT13_CMD_xxx */
	CMD_INT13               = _CMDC(_CMD_DIR_WRITE, _CMD_VTYPE_FC, 32),

	/* logical uplink enable/disable: (u64)a0: 0/1=disable/enable */
	CMD_LOGICAL_UPLINK      = _CMDCNW(_CMD_DIR_WRITE, _CMD_VTYPE_ENET, 33),

	/* undo initialize of virtual link */
	CMD_DEINIT		= _CMDCNW(_CMD_DIR_NONE, _CMD_VTYPE_ALL, 34),

	/* check fw capability of a cmd:
	 * in:  (u32)a0=cmd
	 * out: (u32)a0=errno, 0:valid cmd, a1=supported VNIC_STF_* bits */
	CMD_CAPABILITY      = _CMDC(_CMD_DIR_RW, _CMD_VTYPE_ALL, 36),

	/* persistent binding info
	 * in:  (u64)a0=paddr of arg
	 *      (u32)a1=CMD_PERBI_XXX */
	CMD_PERBI       = _CMDC(_CMD_DIR_RW, _CMD_VTYPE_FC, 37),

	/* Interrupt Assert Register functionality
	 * in: (u16)a0=interrupt number to assert
	 */
	CMD_IAR         = _CMDCNW(_CMD_DIR_WRITE, _CMD_VTYPE_ALL, 38),

	/* initiate hangreset, like softreset after hang detected */
	CMD_HANG_RESET      = _CMDC(_CMD_DIR_NONE, _CMD_VTYPE_ALL, 39),

	/* hangreset status:
	 *    out: a0=0 reset complete, a0=1 reset in progress */
	CMD_HANG_RESET_STATUS   = _CMDC(_CMD_DIR_READ, _CMD_VTYPE_ALL, 40),

	/*
	 * Set hw ingress packet vlan rewrite mode:
	 * in:  (u32)a0=new vlan rewrite mode
	 * out: (u32)a0=old vlan rewrite mode */
	CMD_IG_VLAN_REWRITE_MODE = _CMDC(_CMD_DIR_RW, _CMD_VTYPE_ENET, 41),

	/*
	 * in:  (u16)a0=bdf of target vnic
	 *      (u32)a1=cmd to proxy
	 *      a2-a15=args to cmd in a1
	 * out: (u32)a0=status of proxied cmd
	 *      a1-a15=out args of proxied cmd */
	CMD_PROXY_BY_BDF =  _CMDC(_CMD_DIR_RW, _CMD_VTYPE_ALL, 42),

	/*
	 * As for BY_BDF except a0 is index of hvnlink subordinate vnic
	 * or SR-IOV virtual vnic
	 */
	CMD_PROXY_BY_INDEX =    _CMDC(_CMD_DIR_RW, _CMD_VTYPE_ALL, 43),

	/*
	 * For HPP toggle:
	 * adapter-info-get
	 * in:  (u64)a0=phsical address of buffer passed in from caller.
	 *      (u16)a1=size of buffer specified in a0.
	 * out: (u64)a0=phsical address of buffer passed in from caller.
	 *      (u16)a1=actual bytes from VIF-CONFIG-INFO TLV, or
	 *              0 if no VIF-CONFIG-INFO TLV was ever received. */
	CMD_CONFIG_INFO_GET = _CMDC(_CMD_DIR_RW, _CMD_VTYPE_ALL, 44),

	/*
	 * INT13 API: (u64)a0=paddr to vnic_int13_params struct
	 *            (u32)a1=INT13_CMD_xxx
	 */
	CMD_INT13_ALL = _CMDC(_CMD_DIR_WRITE, _CMD_VTYPE_ALL, 45),

	/*
	 * Set default vlan:
	 * in: (u16)a0=new default vlan
	 *     (u16)a1=zero for overriding vlan with param a0,
	 *             non-zero for resetting vlan to the default
	 * out: (u16)a0=old default vlan
	 */
	CMD_SET_DEFAULT_VLAN = _CMDC(_CMD_DIR_RW, _CMD_VTYPE_ALL, 46),

	/* init_prov_info2:
	 * Variant of CMD_INIT_PROV_INFO, where it will not try to enable
	 * the vnic until CMD_ENABLE2 is issued.
	 *     (u64)a0=paddr of vnic_devcmd_provinfo
	 *     (u32)a1=sizeof provision info
	 */
	CMD_INIT_PROV_INFO2  = _CMDC(_CMD_DIR_WRITE, _CMD_VTYPE_ENET, 47),

	/* enable2:
	 *      (u32)a0=0                  ==> standby
	 *             =CMD_ENABLE2_ACTIVE ==> active
	 */
	CMD_ENABLE2 = _CMDC(_CMD_DIR_WRITE, _CMD_VTYPE_ENET, 48),

	/*
	 * cmd_status:
	 *     Returns the status of the specified command
	 * Input:
	 *     a0 = command for which status is being queried.
	 *          Possible values are:
	 *              CMD_SOFT_RESET
	 *              CMD_HANG_RESET
	 *              CMD_OPEN
	 *              CMD_INIT
	 *              CMD_INIT_PROV_INFO
	 *              CMD_DEINIT
	 *              CMD_INIT_PROV_INFO2
	 *              CMD_ENABLE2
	 * Output:
	 *     if status == STAT_ERROR
	 *        a0 = ERR_ENOTSUPPORTED - status for command in a0 is
	 *                                 not supported
	 *     if status == STAT_NONE
	 *        a0 = status of the devcmd specified in a0 as follows.
	 *             ERR_SUCCESS   - command in a0 completed successfully
	 *             ERR_EINPROGRESS - command in a0 is still in progress
	 */
	CMD_STATUS = _CMDC(_CMD_DIR_RW, _CMD_VTYPE_ALL, 49),

	/*
	 * Returns interrupt coalescing timer conversion factors.
	 * After calling this devcmd, ENIC driver can convert
	 * interrupt coalescing timer in usec into CPU cycles as follows:
	 *
	 *   intr_timer_cycles = intr_timer_usec * multiplier / divisor
	 *
	 * Interrupt coalescing timer in usecs can be be converted/obtained
	 * from CPU cycles as follows:
	 *
	 *   intr_timer_usec = intr_timer_cycles * divisor / multiplier
	 *
	 * in: none
	 * out: (u32)a0 = multiplier
	 *      (u32)a1 = divisor
	 *      (u32)a2 = maximum timer value in usec
	 */
	CMD_INTR_COAL_CONVERT = _CMDC(_CMD_DIR_READ, _CMD_VTYPE_ALL, 50),

	/*
	 * ISCSI DUMP API:
	 * in: (u64)a0=paddr of the param or param itself
	 *     (u32)a1=ISCSI_CMD_xxx
	 */
	CMD_ISCSI_DUMP_REQ = _CMDC(_CMD_DIR_WRITE, _CMD_VTYPE_ALL, 51),

	/*
	 * ISCSI DUMP STATUS API:
	 * in: (u32)a0=cmd tag
	 * in: (u32)a1=ISCSI_CMD_xxx
	 * out: (u32)a0=cmd status
	 */
	CMD_ISCSI_DUMP_STATUS = _CMDC(_CMD_DIR_RW, _CMD_VTYPE_ALL, 52),

	/*
	 * Subvnic migration from MQ <--> VF.
	 * Enable the LIF migration from MQ to VF and vice versa. MQ and VF
	 * indexes are statically bound at the time of initialization.
	 * Based on the
	 * direction of migration, the resources of either MQ or the VF shall
	 * be attached to the LIF.
	 * in:        (u32)a0=Direction of Migration
	 *					0=> Migrate to VF
	 *					1=> Migrate to MQ
	 *            (u32)a1=VF index (MQ index)
	 */
	CMD_MIGRATE_SUBVNIC = _CMDC(_CMD_DIR_WRITE, _CMD_VTYPE_ENET, 53),

	/*
	 * Register / Deregister the notification block for MQ subvnics
	 * in:
	 *   (u64)a0=paddr to notify (set paddr=0 to unset)
	 *   (u32)a1 & 0x00000000ffffffff=sizeof(struct vnic_devcmd_notify)
	 *   (u16)a1 & 0x0000ffff00000000=intr num (-1 for no intr)
	 * out:
	 *   (u32)a1 = effective size
	 */
	CMD_SUBVNIC_NOTIFY = _CMDC(_CMD_DIR_RW, _CMD_VTYPE_ALL, 54),

	/*
	 * Set the predefined mac address as default
	 * in:
	 *   (u48)a0=mac addr
	 */
	CMD_SET_MAC_ADDR = _CMDC(_CMD_DIR_WRITE, _CMD_VTYPE_ENET, 55),

	/* Update the provisioning info of the given VIF
	 *     (u64)a0=paddr of vnic_devcmd_provinfo
	 *     (u32)a1=sizeof provision info
	 */
	CMD_PROV_INFO_UPDATE = _CMDC(_CMD_DIR_WRITE, _CMD_VTYPE_ENET, 56),

	/*
	 * Initialization for the devcmd2 interface.
	 * in: (u64) a0=host result buffer physical address
	 * in: (u16) a1=number of entries in result buffer
	 */
	CMD_INITIALIZE_DEVCMD2 = _CMDC(_CMD_DIR_WRITE, _CMD_VTYPE_ALL, 57)
};

/* flags for CMD_OPEN */
#define CMD_OPENF_OPROM		0x1	/* open coming from option rom */

#define CMD_OPENF_RQ_ENABLE_THEN_POST   0x2

/* flags for CMD_INIT */
#define CMD_INITF_DEFAULT_MAC	0x1	/* init with default mac addr */

/* flags for CMD_PACKET_FILTER */
#define CMD_PFILTER_DIRECTED		0x01
#define CMD_PFILTER_MULTICAST		0x02
#define CMD_PFILTER_BROADCAST		0x04
#define CMD_PFILTER_PROMISCUOUS		0x08
#define CMD_PFILTER_ALL_MULTICAST	0x10

enum vnic_devcmd_status {
	STAT_NONE = 0,
	STAT_BUSY = 1 << 0,	/* cmd in progress */
	STAT_ERROR = 1 << 1,	/* last cmd caused error (code in a0) */
};

enum vnic_devcmd_error {
	ERR_SUCCESS = 0,
	ERR_EINVAL = 1,
	ERR_EFAULT = 2,
	ERR_EPERM = 3,
	ERR_EBUSY = 4,
	ERR_ECMDUNKNOWN = 5,
	ERR_EBADSTATE = 6,
	ERR_ENOMEM = 7,
	ERR_ETIMEDOUT = 8,
	ERR_ELINKDOWN = 9,
};

struct vnic_devcmd_fw_info {
	char fw_version[32];
	char fw_build[32];
	char hw_version[32];
	char hw_serial_number[32];
};

struct vnic_devcmd_notify {
	u32 csum;		/* checksum over following words */

	u32 link_state;		/* link up == 1 */
	u32 port_speed;		/* effective port speed (rate limit) */
	u32 mtu;		/* MTU */
	u32 msglvl;		/* requested driver msg lvl */
	u32 uif;		/* uplink interface */
	u32 status;		/* status bits (see VNIC_STF_*) */
	u32 error;		/* error code (see ERR_*) for first ERR */
	u32 link_down_cnt;	/* running count of link down transitions */
};
#define VNIC_STF_FATAL_ERR	0x0001	/* fatal fw error */

struct vnic_devcmd_provinfo {
	u8 oui[3];
	u8 type;
	u8 data[];
};

/*
 * Writing cmd register causes STAT_BUSY to get set in status register.
 * When cmd completes, STAT_BUSY will be cleared.
 *
 * If cmd completed successfully STAT_ERROR will be clear
 * and args registers contain cmd-specific results.
 *
 * If cmd error, STAT_ERROR will be set and args[0] contains error code.
 *
 * status register is read-only.  While STAT_BUSY is set,
 * all other register contents are read-only.
 */

/* Make sizeof(vnic_devcmd) a power-of-2 for I/O BAR. */
#define VNIC_DEVCMD_NARGS 15
struct vnic_devcmd {
	u32 status;			/* RO */
	u32 cmd;			/* RW */
	u64 args[VNIC_DEVCMD_NARGS];	/* RW cmd args (little-endian) */
};

/*
 * Version 2 of the interface.
 *
 * Some things are carried over, notably the vnic_devcmd_cmd enum.
 */

/*
 * Flags for vnic_devcmd2.flags
 */

#define DEVCMD2_FNORESULT	0x1 /* Don't copy result to host */

#define VNIC_DEVCMD2_NARGS			VNIC_DEVCMD_NARGS

struct vnic_devcmd2 {
	u16 pad;
	u16 flags;
	u32 cmd;                /* same command #defines as original */
	u64 args[VNIC_DEVCMD2_NARGS];
};

#define VNIC_DEVCMD2_NRESULTS			VNIC_DEVCMD_NARGS
struct devcmd2_result {
	u64 results[VNIC_DEVCMD2_NRESULTS];
	u32 pad;
	u16 completed_index;    /* into copy WQ */
	u8  error;              /* same error codes as original */
	u8  color;              /* 0 or 1 as with completion queues */
};

#define DEVCMD2_RING_SIZE			32
#define DEVCMD2_DESC_SIZE			128

#define DEVCMD2_RESULTS_SIZE_MAX		((1 << 16) - 1)

#endif /* _VNIC_DEVCMD_H_ */
