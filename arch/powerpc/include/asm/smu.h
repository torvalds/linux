/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _SMU_H
#define _SMU_H

/*
 * Definitions for talking to the SMU chip in newer G5 PowerMacs
 */
#ifdef __KERNEL__
#include <linux/list.h>
#endif
#include <linux/types.h>

/*
 * Known SMU commands
 *
 * Most of what is below comes from looking at the Open Firmware driver,
 * though this is still incomplete and could use better documentation here
 * or there...
 */


/*
 * Partition info commands
 *
 * These commands are used to retrieve the sdb-partition-XX datas from
 * the SMU. The length is always 2. First byte is the subcommand code
 * and second byte is the partition ID.
 *
 * The reply is 6 bytes:
 *
 *  - 0..1 : partition address
 *  - 2    : a byte containing the partition ID
 *  - 3    : length (maybe other bits are rest of header ?)
 *
 * The data must then be obtained with calls to another command:
 * SMU_CMD_MISC_ee_GET_DATABLOCK_REC (described below).
 */
#define SMU_CMD_PARTITION_COMMAND		0x3e
#define   SMU_CMD_PARTITION_LATEST		0x01
#define   SMU_CMD_PARTITION_BASE		0x02
#define   SMU_CMD_PARTITION_UPDATE		0x03


/*
 * Fan control
 *
 * This is a "mux" for fan control commands. The command seem to
 * act differently based on the number of arguments. With 1 byte
 * of argument, this seem to be queries for fans status, setpoint,
 * etc..., while with 0xe arguments, we will set the fans speeds.
 *
 * Queries (1 byte arg):
 * ---------------------
 *
 * arg=0x01: read RPM fans status
 * arg=0x02: read RPM fans setpoint
 * arg=0x11: read PWM fans status
 * arg=0x12: read PWM fans setpoint
 *
 * the "status" queries return the current speed while the "setpoint" ones
 * return the programmed/target speed. It _seems_ that the result is a bit
 * mask in the first byte of active/available fans, followed by 6 words (16
 * bits) containing the requested speed.
 *
 * Setpoint (14 bytes arg):
 * ------------------------
 *
 * first arg byte is 0 for RPM fans and 0x10 for PWM. Second arg byte is the
 * mask of fans affected by the command. Followed by 6 words containing the
 * setpoint value for selected fans in the mask (or 0 if mask value is 0)
 */
#define SMU_CMD_FAN_COMMAND			0x4a


/*
 * Battery access
 *
 * Same command number as the PMU, could it be same syntax ?
 */
#define SMU_CMD_BATTERY_COMMAND			0x6f
#define   SMU_CMD_GET_BATTERY_INFO		0x00

/*
 * Real time clock control
 *
 * This is a "mux", first data byte contains the "sub" command.
 * The "RTC" part of the SMU controls the date, time, powerup
 * timer, but also a PRAM
 *
 * Dates are in BCD format on 7 bytes:
 * [sec] [min] [hour] [weekday] [month day] [month] [year]
 * with month being 1 based and year minus 100
 */
#define SMU_CMD_RTC_COMMAND			0x8e
#define   SMU_CMD_RTC_SET_PWRUP_TIMER		0x00 /* i: 7 bytes date */
#define   SMU_CMD_RTC_GET_PWRUP_TIMER		0x01 /* o: 7 bytes date */
#define   SMU_CMD_RTC_STOP_PWRUP_TIMER		0x02
#define   SMU_CMD_RTC_SET_PRAM_BYTE_ACC		0x20 /* i: 1 byte (address?) */
#define   SMU_CMD_RTC_SET_PRAM_AUTOINC		0x21 /* i: 1 byte (data?) */
#define   SMU_CMD_RTC_SET_PRAM_LO_BYTES 	0x22 /* i: 10 bytes */
#define   SMU_CMD_RTC_SET_PRAM_HI_BYTES 	0x23 /* i: 10 bytes */
#define   SMU_CMD_RTC_GET_PRAM_BYTE		0x28 /* i: 1 bytes (address?) */
#define   SMU_CMD_RTC_GET_PRAM_LO_BYTES 	0x29 /* o: 10 bytes */
#define   SMU_CMD_RTC_GET_PRAM_HI_BYTES 	0x2a /* o: 10 bytes */
#define	  SMU_CMD_RTC_SET_DATETIME		0x80 /* i: 7 bytes date */
#define   SMU_CMD_RTC_GET_DATETIME		0x81 /* o: 7 bytes date */

 /*
  * i2c commands
  *
  * To issue an i2c command, first is to send a parameter block to
  * the SMU. This is a command of type 0x9a with 9 bytes of header
  * eventually followed by data for a write:
  *
  * 0: bus number (from device-tree usually, SMU has lots of busses !)
  * 1: transfer type/format (see below)
  * 2: device address. For combined and combined4 type transfers, this
  *    is the "write" version of the address (bit 0x01 cleared)
  * 3: subaddress length (0..3)
  * 4: subaddress byte 0 (or only byte for subaddress length 1)
  * 5: subaddress byte 1
  * 6: subaddress byte 2
  * 7: combined address (device address for combined mode data phase)
  * 8: data length
  *
  * The transfer types are the same good old Apple ones it seems,
  * that is:
  *   - 0x00: Simple transfer
  *   - 0x01: Subaddress transfer (addr write + data tx, no restart)
  *   - 0x02: Combined transfer (addr write + restart + data tx)
  *
  * This is then followed by actual data for a write.
  *
  * At this point, the OF driver seems to have a limitation on transfer
  * sizes of 0xd bytes on reads and 0x5 bytes on writes. I do not know
  * whether this is just an OF limit due to some temporary buffer size
  * or if this is an SMU imposed limit. This driver has the same limitation
  * for now as I use a 0x10 bytes temporary buffer as well
  *
  * Once that is completed, a response is expected from the SMU. This is
  * obtained via a command of type 0x9a with a length of 1 byte containing
  * 0 as the data byte. OF also fills the rest of the data buffer with 0xff's
  * though I can't tell yet if this is actually necessary. Once this command
  * is complete, at this point, all I can tell is what OF does. OF tests
  * byte 0 of the reply:
  *   - on read, 0xfe or 0xfc : bus is busy, wait (see below) or nak ?
  *   - on read, 0x00 or 0x01 : reply is in buffer (after the byte 0)
  *   - on write, < 0 -> failure (immediate exit)
  *   - else, OF just exists (without error, weird)
  *
  * So on read, there is this wait-for-busy thing when getting a 0xfc or
  * 0xfe result. OF does a loop of up to 64 retries, waiting 20ms and
  * doing the above again until either the retries expire or the result
  * is no longer 0xfe or 0xfc
  *
  * The Darwin I2C driver is less subtle though. On any non-success status
  * from the response command, it waits 5ms and tries again up to 20 times,
  * it doesn't differentiate between fatal errors or "busy" status.
  *
  * This driver provides an asynchronous paramblock based i2c command
  * interface to be used either directly by low level code or by a higher
  * level driver interfacing to the linux i2c layer. The current
  * implementation of this relies on working timers & timer interrupts
  * though, so be careful of calling context for now. This may be "fixed"
  * in the future by adding a polling facility.
  */
#define SMU_CMD_I2C_COMMAND			0x9a
          /* transfer types */
#define   SMU_I2C_TRANSFER_SIMPLE	0x00
#define   SMU_I2C_TRANSFER_STDSUB	0x01
#define   SMU_I2C_TRANSFER_COMBINED	0x02

/*
 * Power supply control
 *
 * The "sub" command is an ASCII string in the data, the
 * data length is that of the string.
 *
 * The VSLEW command can be used to get or set the voltage slewing.
 *  - length 5 (only "VSLEW") : it returns "DONE" and 3 bytes of
 *    reply at data offset 6, 7 and 8.
 *  - length 8 ("VSLEWxyz") has 3 additional bytes appended, and is
 *    used to set the voltage slewing point. The SMU replies with "DONE"
 * I yet have to figure out their exact meaning of those 3 bytes in
 * both cases. They seem to be:
 *  x = processor mask
 *  y = op. point index
 *  z = processor freq. step index
 * I haven't yet deciphered result codes
 *
 */
#define SMU_CMD_POWER_COMMAND			0xaa
#define   SMU_CMD_POWER_RESTART		       	"RESTART"
#define   SMU_CMD_POWER_SHUTDOWN		"SHUTDOWN"
#define   SMU_CMD_POWER_VOLTAGE_SLEW		"VSLEW"

/*
 * Read ADC sensors
 *
 * This command takes one byte of parameter: the sensor ID (or "reg"
 * value in the device-tree) and returns a 16 bits value
 */
#define SMU_CMD_READ_ADC			0xd8


/* Misc commands
 *
 * This command seem to be a grab bag of various things
 *
 * Parameters:
 *   1: subcommand
 */
#define SMU_CMD_MISC_df_COMMAND			0xdf

/*
 * Sets "system ready" status
 *
 * I did not yet understand how it exactly works or what it does.
 *
 * Guessing from OF code, 0x02 activates the display backlight. Apple uses/used
 * the same codebase for all OF versions. On PowerBooks, this command would
 * enable the backlight. For the G5s, it only activates the front LED. However,
 * don't take this for granted.
 *
 * Parameters:
 *   2: status [0x00, 0x01 or 0x02]
 */
#define   SMU_CMD_MISC_df_SET_DISPLAY_LIT	0x02

/*
 * Sets mode of power switch.
 *
 * What this actually does is not yet known. Maybe it enables some interrupt.
 *
 * Parameters:
 *   2: enable power switch? [0x00 or 0x01]
 *   3 (optional): enable nmi? [0x00 or 0x01]
 *
 * Returns:
 *   If parameter 2 is 0x00 and parameter 3 is not specified, returns whether
 *   NMI is enabled. Otherwise unknown.
 */
#define   SMU_CMD_MISC_df_NMI_OPTION		0x04

/* Sets LED dimm offset.
 *
 * The front LED dimms itself during sleep. Its brightness (or, well, the PWM
 * frequency) depends on current time. Therefore, the SMU needs to know the
 * timezone.
 *
 * Parameters:
 *   2-8: unknown (BCD coding)
 */
#define   SMU_CMD_MISC_df_DIMM_OFFSET		0x99


/*
 * Version info commands
 *
 * Parameters:
 *   1 (optional): Specifies version part to retrieve
 *
 * Returns:
 *   Version value
 */
#define SMU_CMD_VERSION_COMMAND			0xea
#define   SMU_VERSION_RUNNING			0x00
#define   SMU_VERSION_BASE			0x01
#define   SMU_VERSION_UPDATE			0x02


/*
 * Switches
 *
 * These are switches whose status seems to be known to the SMU.
 *
 * Parameters:
 *   none
 *
 * Result:
 *   Switch bits (ORed, see below)
 */
#define SMU_CMD_SWITCHES			0xdc

/* Switches bits */
#define SMU_SWITCH_CASE_CLOSED			0x01
#define SMU_SWITCH_AC_POWER			0x04
#define SMU_SWITCH_POWER_SWITCH			0x08


/*
 * Misc commands
 *
 * This command seem to be a grab bag of various things
 *
 * SMU_CMD_MISC_ee_GET_DATABLOCK_REC is used, among others, to
 * transfer blocks of data from the SMU. So far, I've decrypted it's
 * usage to retrieve partition data. In order to do that, you have to
 * break your transfer in "chunks" since that command cannot transfer
 * more than a chunk at a time. The chunk size used by OF is 0xe bytes,
 * but it seems that the darwin driver will let you do 0x1e bytes if
 * your "PMU" version is >= 0x30. You can get the "PMU" version apparently
 * either in the last 16 bits of property "smu-version-pmu" or as the 16
 * bytes at offset 1 of "smu-version-info"
 *
 * For each chunk, the command takes 7 bytes of arguments:
 *  byte 0: subcommand code (0x02)
 *  byte 1: 0x04 (always, I don't know what it means, maybe the address
 *                space to use or some other nicety. It's hard coded in OF)
 *  byte 2..5: SMU address of the chunk (big endian 32 bits)
 *  byte 6: size to transfer (up to max chunk size)
 *
 * The data is returned directly
 */
#define SMU_CMD_MISC_ee_COMMAND			0xee
#define   SMU_CMD_MISC_ee_GET_DATABLOCK_REC	0x02

/* Retrieves currently used watts.
 *
 * Parameters:
 *   1: 0x03 (Meaning unknown)
 */
#define   SMU_CMD_MISC_ee_GET_WATTS		0x03

#define   SMU_CMD_MISC_ee_LEDS_CTRL		0x04 /* i: 00 (00,01) [00] */
#define   SMU_CMD_MISC_ee_GET_DATA		0x05 /* i: 00 , o: ?? */


/*
 * Power related commands
 *
 * Parameters:
 *   1: subcommand
 */
#define SMU_CMD_POWER_EVENTS_COMMAND		0x8f

/* SMU_POWER_EVENTS subcommands */
enum {
	SMU_PWR_GET_POWERUP_EVENTS      = 0x00,
	SMU_PWR_SET_POWERUP_EVENTS      = 0x01,
	SMU_PWR_CLR_POWERUP_EVENTS      = 0x02,
	SMU_PWR_GET_WAKEUP_EVENTS       = 0x03,
	SMU_PWR_SET_WAKEUP_EVENTS       = 0x04,
	SMU_PWR_CLR_WAKEUP_EVENTS       = 0x05,

	/*
	 * Get last shutdown cause
	 *
	 * Returns:
	 *   1 byte (signed char): Last shutdown cause. Exact meaning unknown.
	 */
	SMU_PWR_LAST_SHUTDOWN_CAUSE	= 0x07,

	/*
	 * Sets or gets server ID. Meaning or use is unknown.
	 *
	 * Parameters:
	 *   2 (optional): Set server ID (1 byte)
	 *
	 * Returns:
	 *   1 byte (server ID?)
	 */
	SMU_PWR_SERVER_ID		= 0x08,
};

/* Power events wakeup bits */
enum {
	SMU_PWR_WAKEUP_KEY              = 0x01, /* Wake on key press */
	SMU_PWR_WAKEUP_AC_INSERT        = 0x02, /* Wake on AC adapter plug */
	SMU_PWR_WAKEUP_AC_CHANGE        = 0x04,
	SMU_PWR_WAKEUP_LID_OPEN         = 0x08,
	SMU_PWR_WAKEUP_RING             = 0x10,
};


/*
 * - Kernel side interface -
 */

#ifdef __KERNEL__

/*
 * Asynchronous SMU commands
 *
 * Fill up this structure and submit it via smu_queue_command(),
 * and get notified by the optional done() callback, or because
 * status becomes != 1
 */

struct smu_cmd;

struct smu_cmd
{
	/* public */
	u8			cmd;		/* command */
	int			data_len;	/* data len */
	int			reply_len;	/* reply len */
	void			*data_buf;	/* data buffer */
	void			*reply_buf;	/* reply buffer */
	int			status;		/* command status */
	void			(*done)(struct smu_cmd *cmd, void *misc);
	void			*misc;

	/* private */
	struct list_head	link;
};

/*
 * Queues an SMU command, all fields have to be initialized
 */
extern int smu_queue_cmd(struct smu_cmd *cmd);

/*
 * Simple command wrapper. This structure embeds a small buffer
 * to ease sending simple SMU commands from the stack
 */
struct smu_simple_cmd
{
	struct smu_cmd	cmd;
	u8	       	buffer[16];
};

/*
 * Queues a simple command. All fields will be initialized by that
 * function
 */
extern int smu_queue_simple(struct smu_simple_cmd *scmd, u8 command,
			    unsigned int data_len,
			    void (*done)(struct smu_cmd *cmd, void *misc),
			    void *misc,
			    ...);

/*
 * Completion helper. Pass it to smu_queue_simple or as 'done'
 * member to smu_queue_cmd, it will call complete() on the struct
 * completion passed in the "misc" argument
 */
extern void smu_done_complete(struct smu_cmd *cmd, void *misc);

/*
 * Synchronous helpers. Will spin-wait for completion of a command
 */
extern void smu_spinwait_cmd(struct smu_cmd *cmd);

static inline void smu_spinwait_simple(struct smu_simple_cmd *scmd)
{
	smu_spinwait_cmd(&scmd->cmd);
}

/*
 * Poll routine to call if blocked with irqs off
 */
extern void smu_poll(void);


/*
 * Init routine, presence check....
 */
int __init smu_init(void);
extern int smu_present(void);
struct platform_device;
extern struct platform_device *smu_get_ofdev(void);


/*
 * Common command wrappers
 */
extern void smu_shutdown(void);
extern void smu_restart(void);
struct rtc_time;
extern int smu_get_rtc_time(struct rtc_time *time, int spinwait);
extern int smu_set_rtc_time(struct rtc_time *time, int spinwait);

/*
 * Kernel asynchronous i2c interface
 */

#define SMU_I2C_READ_MAX	0x1d
#define SMU_I2C_WRITE_MAX	0x15

/* SMU i2c header, exactly matches i2c header on wire */
struct smu_i2c_param
{
	u8	bus;		/* SMU bus ID (from device tree) */
	u8	type;		/* i2c transfer type */
	u8	devaddr;	/* device address (includes direction) */
	u8	sublen;		/* subaddress length */
	u8	subaddr[3];	/* subaddress */
	u8	caddr;		/* combined address, filled by SMU driver */
	u8	datalen;	/* length of transfer */
	u8	data[SMU_I2C_READ_MAX];	/* data */
};

struct smu_i2c_cmd
{
	/* public */
	struct smu_i2c_param	info;
	void			(*done)(struct smu_i2c_cmd *cmd, void *misc);
	void			*misc;
	int			status; /* 1 = pending, 0 = ok, <0 = fail */

	/* private */
	struct smu_cmd		scmd;
	int			read;
	int			stage;
	int			retries;
	u8			pdata[32];
	struct list_head	link;
};

/*
 * Call this to queue an i2c command to the SMU. You must fill info,
 * including info.data for a write, done and misc.
 * For now, no polling interface is provided so you have to use completion
 * callback.
 */
extern int smu_queue_i2c(struct smu_i2c_cmd *cmd);


#endif /* __KERNEL__ */


/*
 * - SMU "sdb" partitions informations -
 */


/*
 * Partition header format
 */
struct smu_sdbp_header {
	__u8	id;
	__u8	len;
	__u8	version;
	__u8	flags;
};


 /*
 * demangle 16 and 32 bits integer in some SMU partitions
 * (currently, afaik, this concerns only the FVT partition
 * (0x12)
 */
#define SMU_U16_MIX(x)	le16_to_cpu(x)
#define SMU_U32_MIX(x)  ((((x) & 0xff00ff00u) >> 8)|(((x) & 0x00ff00ffu) << 8))


/* This is the definition of the SMU sdb-partition-0x12 table (called
 * CPU F/V/T operating points in Darwin). The definition for all those
 * SMU tables should be moved to some separate file
 */
#define SMU_SDB_FVT_ID			0x12

struct smu_sdbp_fvt {
	__u32	sysclk;			/* Base SysClk frequency in Hz for
					 * this operating point. Value need to
					 * be unmixed with SMU_U32_MIX()
					 */
	__u8	pad;
	__u8	maxtemp;		/* Max temp. supported by this
					 * operating point
					 */

	__u16	volts[3];		/* CPU core voltage for the 3
					 * PowerTune modes, a mode with
					 * 0V = not supported. Value need
					 * to be unmixed with SMU_U16_MIX()
					 */
};

/* This partition contains voltage & current sensor calibration
 * informations
 */
#define SMU_SDB_CPUVCP_ID		0x21

struct smu_sdbp_cpuvcp {
	__u16	volt_scale;		/* u4.12 fixed point */
	__s16	volt_offset;		/* s4.12 fixed point */
	__u16	curr_scale;		/* u4.12 fixed point */
	__s16	curr_offset;		/* s4.12 fixed point */
	__s32	power_quads[3];		/* s4.28 fixed point */
};

/* This partition contains CPU thermal diode calibration
 */
#define SMU_SDB_CPUDIODE_ID		0x18

struct smu_sdbp_cpudiode {
	__u16	m_value;		/* u1.15 fixed point */
	__s16	b_value;		/* s10.6 fixed point */

};

/* This partition contains Slots power calibration
 */
#define SMU_SDB_SLOTSPOW_ID		0x78

struct smu_sdbp_slotspow {
	__u16	pow_scale;		/* u4.12 fixed point */
	__s16	pow_offset;		/* s4.12 fixed point */
};

/* This partition contains machine specific version information about
 * the sensor/control layout
 */
#define SMU_SDB_SENSORTREE_ID		0x25

struct smu_sdbp_sensortree {
	__u8	model_id;
	__u8	unknown[3];
};

/* This partition contains CPU thermal control PID informations. So far
 * only single CPU machines have been seen with an SMU, so we assume this
 * carries only informations for those
 */
#define SMU_SDB_CPUPIDDATA_ID		0x17

struct smu_sdbp_cpupiddata {
	__u8	unknown1;
	__u8	target_temp_delta;
	__u8	unknown2;
	__u8	history_len;
	__s16	power_adj;
	__u16	max_power;
	__s32	gp,gr,gd;
};


/* Other partitions without known structures */
#define SMU_SDB_DEBUG_SWITCHES_ID	0x05

#ifdef __KERNEL__
/*
 * This returns the pointer to an SMU "sdb" partition data or NULL
 * if not found. The data format is described below
 */
extern const struct smu_sdbp_header *smu_get_sdb_partition(int id,
					unsigned int *size);

/* Get "sdb" partition data from an SMU satellite */
extern struct smu_sdbp_header *smu_sat_get_sdb_partition(unsigned int sat_id,
					int id, unsigned int *size);


#endif /* __KERNEL__ */


/*
 * - Userland interface -
 */

/*
 * A given instance of the device can be configured for 2 different
 * things at the moment:
 *
 *  - sending SMU commands (default at open() time)
 *  - receiving SMU events (not yet implemented)
 *
 * Commands are written with write() of a command block. They can be
 * "driver" commands (for example to switch to event reception mode)
 * or real SMU commands. They are made of a header followed by command
 * data if any.
 *
 * For SMU commands (not for driver commands), you can then read() back
 * a reply. The reader will be blocked or not depending on how the device
 * file is opened. poll() isn't implemented yet. The reply will consist
 * of a header as well, followed by the reply data if any. You should
 * always provide a buffer large enough for the maximum reply data, I
 * recommand one page.
 *
 * It is illegal to send SMU commands through a file descriptor configured
 * for events reception
 *
 */
struct smu_user_cmd_hdr
{
	__u32		cmdtype;
#define SMU_CMDTYPE_SMU			0	/* SMU command */
#define SMU_CMDTYPE_WANTS_EVENTS	1	/* switch fd to events mode */
#define SMU_CMDTYPE_GET_PARTITION	2	/* retrieve an sdb partition */

	__u8		cmd;			/* SMU command byte */
	__u8		pad[3];			/* padding */
	__u32		data_len;		/* Length of data following */
};

struct smu_user_reply_hdr
{
	__u32		status;			/* Command status */
	__u32		reply_len;		/* Length of data follwing */
};

#endif /*  _SMU_H */
