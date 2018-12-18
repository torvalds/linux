// SPDX-License-Identifier: GPL-2.0+

/*
 * Multifunction core driver for Zodiac Inflight Innovations RAVE
 * Supervisory Processor(SP) MCU that is connected via dedicated UART
 * port
 *
 * Copyright (C) 2017 Zodiac Inflight Innovations
 */

#include <linux/atomic.h>
#include <linux/crc-ccitt.h>
#include <linux/delay.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/mfd/rave-sp.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/sched.h>
#include <linux/serdev.h>
#include <asm/unaligned.h>

/*
 * UART protocol using following entities:
 *  - message to MCU => ACK response
 *  - event from MCU => event ACK
 *
 * Frame structure:
 * <STX> <DATA> <CHECKSUM> <ETX>
 * Where:
 * - STX - is start of transmission character
 * - ETX - end of transmission
 * - DATA - payload
 * - CHECKSUM - checksum calculated on <DATA>
 *
 * If <DATA> or <CHECKSUM> contain one of control characters, then it is
 * escaped using <DLE> control code. Added <DLE> does not participate in
 * checksum calculation.
 */
#define RAVE_SP_STX			0x02
#define RAVE_SP_ETX			0x03
#define RAVE_SP_DLE			0x10

#define RAVE_SP_MAX_DATA_SIZE		64
#define RAVE_SP_CHECKSUM_8B2C		1
#define RAVE_SP_CHECKSUM_CCITT		2
#define RAVE_SP_CHECKSUM_SIZE		RAVE_SP_CHECKSUM_CCITT
/*
 * We don't store STX, ETX and unescaped bytes, so Rx is only
 * DATA + CSUM
 */
#define RAVE_SP_RX_BUFFER_SIZE				\
	(RAVE_SP_MAX_DATA_SIZE + RAVE_SP_CHECKSUM_SIZE)

#define RAVE_SP_STX_ETX_SIZE		2
/*
 * For Tx we have to have space for everything, STX, EXT and
 * potentially stuffed DATA + CSUM data + csum
 */
#define RAVE_SP_TX_BUFFER_SIZE				\
	(RAVE_SP_STX_ETX_SIZE + 2 * RAVE_SP_RX_BUFFER_SIZE)

/**
 * enum rave_sp_deframer_state - Possible state for de-framer
 *
 * @RAVE_SP_EXPECT_SOF:		 Scanning input for start-of-frame marker
 * @RAVE_SP_EXPECT_DATA:	 Got start of frame marker, collecting frame
 * @RAVE_SP_EXPECT_ESCAPED_DATA: Got escape character, collecting escaped byte
 */
enum rave_sp_deframer_state {
	RAVE_SP_EXPECT_SOF,
	RAVE_SP_EXPECT_DATA,
	RAVE_SP_EXPECT_ESCAPED_DATA,
};

/**
 * struct rave_sp_deframer - Device protocol deframer
 *
 * @state:  Current state of the deframer
 * @data:   Buffer used to collect deframed data
 * @length: Number of bytes de-framed so far
 */
struct rave_sp_deframer {
	enum rave_sp_deframer_state state;
	unsigned char data[RAVE_SP_RX_BUFFER_SIZE];
	size_t length;
};

/**
 * struct rave_sp_reply - Reply as per RAVE device protocol
 *
 * @length:	Expected reply length
 * @data:	Buffer to store reply payload in
 * @code:	Expected reply code
 * @ackid:	Expected reply ACK ID
 * @completion: Successful reply reception completion
 */
struct rave_sp_reply {
	size_t length;
	void  *data;
	u8     code;
	u8     ackid;
	struct completion received;
};

/**
 * struct rave_sp_checksum - Variant specific checksum implementation details
 *
 * @length:	Caculated checksum length
 * @subroutine:	Utilized checksum algorithm implementation
 */
struct rave_sp_checksum {
	size_t length;
	void (*subroutine)(const u8 *, size_t, u8 *);
};

struct rave_sp_version {
	u8     hardware;
	__le16 major;
	u8     minor;
	u8     letter[2];
} __packed;

struct rave_sp_status {
	struct rave_sp_version bootloader_version;
	struct rave_sp_version firmware_version;
	u16 rdu_eeprom_flag;
	u16 dds_eeprom_flag;
	u8  pic_flag;
	u8  orientation;
	u32 etc;
	s16 temp[2];
	u8  backlight_current[3];
	u8  dip_switch;
	u8  host_interrupt;
	u16 voltage_28;
	u8  i2c_device_status;
	u8  power_status;
	u8  general_status;
	u8  deprecated1;
	u8  power_led_status;
	u8  deprecated2;
	u8  periph_power_shutoff;
} __packed;

/**
 * struct rave_sp_variant_cmds - Variant specific command routines
 *
 * @translate:	Generic to variant specific command mapping routine
 * @get_status: Variant specific implementation of CMD_GET_STATUS
 */
struct rave_sp_variant_cmds {
	int (*translate)(enum rave_sp_command);
	int (*get_status)(struct rave_sp *sp, struct rave_sp_status *);
};

/**
 * struct rave_sp_variant - RAVE supervisory processor core variant
 *
 * @checksum:	Variant specific checksum implementation
 * @cmd:	Variant specific command pointer table
 *
 */
struct rave_sp_variant {
	const struct rave_sp_checksum *checksum;
	struct rave_sp_variant_cmds cmd;
};

/**
 * struct rave_sp - RAVE supervisory processor core
 *
 * @serdev:			Pointer to underlying serdev
 * @deframer:			Stored state of the protocol deframer
 * @ackid:			ACK ID used in last reply sent to the device
 * @bus_lock:			Lock to serialize access to the device
 * @reply_lock:			Lock protecting @reply
 * @reply:			Pointer to memory to store reply payload
 *
 * @variant:			Device variant specific information
 * @event_notifier_list:	Input event notification chain
 *
 * @part_number_firmware:	Firmware version
 * @part_number_bootloader:	Bootloader version
 */
struct rave_sp {
	struct serdev_device *serdev;
	struct rave_sp_deframer deframer;
	atomic_t ackid;
	struct mutex bus_lock;
	struct mutex reply_lock;
	struct rave_sp_reply *reply;

	const struct rave_sp_variant *variant;
	struct blocking_notifier_head event_notifier_list;

	const char *part_number_firmware;
	const char *part_number_bootloader;
};

static bool rave_sp_id_is_event(u8 code)
{
	return (code & 0xF0) == RAVE_SP_EVNT_BASE;
}

static void rave_sp_unregister_event_notifier(struct device *dev, void *res)
{
	struct rave_sp *sp = dev_get_drvdata(dev->parent);
	struct notifier_block *nb = *(struct notifier_block **)res;
	struct blocking_notifier_head *bnh = &sp->event_notifier_list;

	WARN_ON(blocking_notifier_chain_unregister(bnh, nb));
}

int devm_rave_sp_register_event_notifier(struct device *dev,
					 struct notifier_block *nb)
{
	struct rave_sp *sp = dev_get_drvdata(dev->parent);
	struct notifier_block **rcnb;
	int ret;

	rcnb = devres_alloc(rave_sp_unregister_event_notifier,
			    sizeof(*rcnb), GFP_KERNEL);
	if (!rcnb)
		return -ENOMEM;

	ret = blocking_notifier_chain_register(&sp->event_notifier_list, nb);
	if (!ret) {
		*rcnb = nb;
		devres_add(dev, rcnb);
	} else {
		devres_free(rcnb);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(devm_rave_sp_register_event_notifier);

static void csum_8b2c(const u8 *buf, size_t size, u8 *crc)
{
	*crc = *buf++;
	size--;

	while (size--)
		*crc += *buf++;

	*crc = 1 + ~(*crc);
}

static void csum_ccitt(const u8 *buf, size_t size, u8 *crc)
{
	const u16 calculated = crc_ccitt_false(0xffff, buf, size);

	/*
	 * While the rest of the wire protocol is little-endian,
	 * CCITT-16 CRC in RDU2 device is sent out in big-endian order.
	 */
	put_unaligned_be16(calculated, crc);
}

static void *stuff(unsigned char *dest, const unsigned char *src, size_t n)
{
	while (n--) {
		const unsigned char byte = *src++;

		switch (byte) {
		case RAVE_SP_STX:
		case RAVE_SP_ETX:
		case RAVE_SP_DLE:
			*dest++ = RAVE_SP_DLE;
			/* FALLTHROUGH */
		default:
			*dest++ = byte;
		}
	}

	return dest;
}

static int rave_sp_write(struct rave_sp *sp, const u8 *data, u8 data_size)
{
	const size_t checksum_length = sp->variant->checksum->length;
	unsigned char frame[RAVE_SP_TX_BUFFER_SIZE];
	unsigned char crc[RAVE_SP_CHECKSUM_SIZE];
	unsigned char *dest = frame;
	size_t length;

	if (WARN_ON(checksum_length > sizeof(crc)))
		return -ENOMEM;

	if (WARN_ON(data_size > sizeof(frame)))
		return -ENOMEM;

	sp->variant->checksum->subroutine(data, data_size, crc);

	*dest++ = RAVE_SP_STX;
	dest = stuff(dest, data, data_size);
	dest = stuff(dest, crc, checksum_length);
	*dest++ = RAVE_SP_ETX;

	length = dest - frame;

	print_hex_dump_debug("rave-sp tx: ", DUMP_PREFIX_NONE,
			     16, 1, frame, length, false);

	return serdev_device_write(sp->serdev, frame, length, HZ);
}

static u8 rave_sp_reply_code(u8 command)
{
	/*
	 * There isn't a single rule that describes command code ->
	 * ACK code transformation, but, going through various
	 * versions of ICDs, there appear to be three distinct groups
	 * that can be described by simple transformation.
	 */
	switch (command) {
	case 0xA0 ... 0xBE:
		/*
		 * Commands implemented by firmware found in RDU1 and
		 * older devices all seem to obey the following rule
		 */
		return command + 0x20;
	case 0xE0 ... 0xEF:
		/*
		 * Events emitted by all versions of the firmare use
		 * least significant bit to get an ACK code
		 */
		return command | 0x01;
	default:
		/*
		 * Commands implemented by firmware found in RDU2 are
		 * similar to "old" commands, but they use slightly
		 * different offset
		 */
		return command + 0x40;
	}
}

int rave_sp_exec(struct rave_sp *sp,
		 void *__data,  size_t data_size,
		 void *reply_data, size_t reply_data_size)
{
	struct rave_sp_reply reply = {
		.data     = reply_data,
		.length   = reply_data_size,
		.received = COMPLETION_INITIALIZER_ONSTACK(reply.received),
	};
	unsigned char *data = __data;
	int command, ret = 0;
	u8 ackid;

	command = sp->variant->cmd.translate(data[0]);
	if (command < 0)
		return command;

	ackid       = atomic_inc_return(&sp->ackid);
	reply.ackid = ackid;
	reply.code  = rave_sp_reply_code((u8)command),

	mutex_lock(&sp->bus_lock);

	mutex_lock(&sp->reply_lock);
	sp->reply = &reply;
	mutex_unlock(&sp->reply_lock);

	data[0] = command;
	data[1] = ackid;

	rave_sp_write(sp, data, data_size);

	if (!wait_for_completion_timeout(&reply.received, HZ)) {
		dev_err(&sp->serdev->dev, "Command timeout\n");
		ret = -ETIMEDOUT;

		mutex_lock(&sp->reply_lock);
		sp->reply = NULL;
		mutex_unlock(&sp->reply_lock);
	}

	mutex_unlock(&sp->bus_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(rave_sp_exec);

static void rave_sp_receive_event(struct rave_sp *sp,
				  const unsigned char *data, size_t length)
{
	u8 cmd[] = {
		[0] = rave_sp_reply_code(data[0]),
		[1] = data[1],
	};

	rave_sp_write(sp, cmd, sizeof(cmd));

	blocking_notifier_call_chain(&sp->event_notifier_list,
				     rave_sp_action_pack(data[0], data[2]),
				     NULL);
}

static void rave_sp_receive_reply(struct rave_sp *sp,
				  const unsigned char *data, size_t length)
{
	struct device *dev = &sp->serdev->dev;
	struct rave_sp_reply *reply;
	const  size_t payload_length = length - 2;

	mutex_lock(&sp->reply_lock);
	reply = sp->reply;

	if (reply) {
		if (reply->code == data[0] && reply->ackid == data[1] &&
		    payload_length >= reply->length) {
			/*
			 * We are relying on memcpy(dst, src, 0) to be a no-op
			 * when handling commands that have a no-payload reply
			 */
			memcpy(reply->data, &data[2], reply->length);
			complete(&reply->received);
			sp->reply = NULL;
		} else {
			dev_err(dev, "Ignoring incorrect reply\n");
			dev_dbg(dev, "Code:   expected = 0x%08x received = 0x%08x\n",
				reply->code, data[0]);
			dev_dbg(dev, "ACK ID: expected = 0x%08x received = 0x%08x\n",
				reply->ackid, data[1]);
			dev_dbg(dev, "Length: expected = %zu received = %zu\n",
				reply->length, payload_length);
		}
	}

	mutex_unlock(&sp->reply_lock);
}

static void rave_sp_receive_frame(struct rave_sp *sp,
				  const unsigned char *data,
				  size_t length)
{
	const size_t checksum_length = sp->variant->checksum->length;
	const size_t payload_length  = length - checksum_length;
	const u8 *crc_reported       = &data[payload_length];
	struct device *dev           = &sp->serdev->dev;
	u8 crc_calculated[RAVE_SP_CHECKSUM_SIZE];

	if (unlikely(checksum_length > sizeof(crc_calculated))) {
		dev_warn(dev, "Checksum too long, dropping\n");
		return;
	}

	print_hex_dump_debug("rave-sp rx: ", DUMP_PREFIX_NONE,
			     16, 1, data, length, false);

	if (unlikely(length <= checksum_length)) {
		dev_warn(dev, "Dropping short frame\n");
		return;
	}

	sp->variant->checksum->subroutine(data, payload_length,
					  crc_calculated);

	if (memcmp(crc_calculated, crc_reported, checksum_length)) {
		dev_warn(dev, "Dropping bad frame\n");
		return;
	}

	if (rave_sp_id_is_event(data[0]))
		rave_sp_receive_event(sp, data, length);
	else
		rave_sp_receive_reply(sp, data, length);
}

static int rave_sp_receive_buf(struct serdev_device *serdev,
			       const unsigned char *buf, size_t size)
{
	struct device *dev = &serdev->dev;
	struct rave_sp *sp = dev_get_drvdata(dev);
	struct rave_sp_deframer *deframer = &sp->deframer;
	const unsigned char *src = buf;
	const unsigned char *end = buf + size;

	while (src < end) {
		const unsigned char byte = *src++;

		switch (deframer->state) {
		case RAVE_SP_EXPECT_SOF:
			if (byte == RAVE_SP_STX)
				deframer->state = RAVE_SP_EXPECT_DATA;
			break;

		case RAVE_SP_EXPECT_DATA:
			/*
			 * Treat special byte values first
			 */
			switch (byte) {
			case RAVE_SP_ETX:
				rave_sp_receive_frame(sp,
						      deframer->data,
						      deframer->length);
				/*
				 * Once we extracted a complete frame
				 * out of a stream, we call it done
				 * and proceed to bailing out while
				 * resetting the framer to initial
				 * state, regardless if we've consumed
				 * all of the stream or not.
				 */
				goto reset_framer;
			case RAVE_SP_STX:
				dev_warn(dev, "Bad frame: STX before ETX\n");
				/*
				 * If we encounter second "start of
				 * the frame" marker before seeing
				 * corresponding "end of frame", we
				 * reset the framer and ignore both:
				 * frame started by first SOF and
				 * frame started by current SOF.
				 *
				 * NOTE: The above means that only the
				 * frame started by third SOF, sent
				 * after this one will have a chance
				 * to get throught.
				 */
				goto reset_framer;
			case RAVE_SP_DLE:
				deframer->state = RAVE_SP_EXPECT_ESCAPED_DATA;
				/*
				 * If we encounter escape sequence we
				 * need to skip it and collect the
				 * byte that follows. We do it by
				 * forcing the next iteration of the
				 * encompassing while loop.
				 */
				continue;
			}
			/*
			 * For the rest of the bytes, that are not
			 * speical snoflakes, we do the same thing
			 * that we do to escaped data - collect it in
			 * deframer buffer
			 */

			/* FALLTHROUGH */

		case RAVE_SP_EXPECT_ESCAPED_DATA:
			if (deframer->length == sizeof(deframer->data)) {
				dev_warn(dev, "Bad frame: Too long\n");
				/*
				 * If the amount of data we've
				 * accumulated for current frame so
				 * far starts to exceed the capacity
				 * of deframer's buffer, there's
				 * nothing else we can do but to
				 * discard that data and start
				 * assemblying a new frame again
				 */
				goto reset_framer;
			}

			deframer->data[deframer->length++] = byte;

			/*
			 * We've extracted out special byte, now we
			 * can go back to regular data collecting
			 */
			deframer->state = RAVE_SP_EXPECT_DATA;
			break;
		}
	}

	/*
	 * The only way to get out of the above loop and end up here
	 * is throught consuming all of the supplied data, so here we
	 * report that we processed it all.
	 */
	return size;

reset_framer:
	/*
	 * NOTE: A number of codepaths that will drop us here will do
	 * so before consuming all 'size' bytes of the data passed by
	 * serdev layer. We rely on the fact that serdev layer will
	 * re-execute this handler with the remainder of the Rx bytes
	 * once we report actual number of bytes that we processed.
	 */
	deframer->state  = RAVE_SP_EXPECT_SOF;
	deframer->length = 0;

	return src - buf;
}

static int rave_sp_rdu1_cmd_translate(enum rave_sp_command command)
{
	if (command >= RAVE_SP_CMD_STATUS &&
	    command <= RAVE_SP_CMD_CONTROL_EVENTS)
		return command;

	return -EINVAL;
}

static int rave_sp_rdu2_cmd_translate(enum rave_sp_command command)
{
	if (command >= RAVE_SP_CMD_GET_FIRMWARE_VERSION &&
	    command <= RAVE_SP_CMD_GET_GPIO_STATE)
		return command;

	if (command == RAVE_SP_CMD_REQ_COPPER_REV) {
		/*
		 * As per RDU2 ICD 3.4.47 CMD_GET_COPPER_REV code is
		 * different from that for RDU1 and it is set to 0x28.
		 */
		return 0x28;
	}

	return rave_sp_rdu1_cmd_translate(command);
}

static int rave_sp_default_cmd_translate(enum rave_sp_command command)
{
	/*
	 * All of the following command codes were taken from "Table :
	 * Communications Protocol Message Types" in section 3.3
	 * "MESSAGE TYPES" of Rave PIC24 ICD.
	 */
	switch (command) {
	case RAVE_SP_CMD_GET_FIRMWARE_VERSION:
		return 0x11;
	case RAVE_SP_CMD_GET_BOOTLOADER_VERSION:
		return 0x12;
	case RAVE_SP_CMD_BOOT_SOURCE:
		return 0x14;
	case RAVE_SP_CMD_SW_WDT:
		return 0x1C;
	case RAVE_SP_CMD_PET_WDT:
		return 0x1D;
	case RAVE_SP_CMD_RESET:
		return 0x1E;
	case RAVE_SP_CMD_RESET_REASON:
		return 0x1F;
	case RAVE_SP_CMD_RMB_EEPROM:
		return 0x20;
	default:
		return -EINVAL;
	}
}

static const char *devm_rave_sp_version(struct device *dev,
					struct rave_sp_version *version)
{
	/*
	 * NOTE: The format string below uses %02d to display u16
	 * intentionally for the sake of backwards compatibility with
	 * legacy software.
	 */
	return devm_kasprintf(dev, GFP_KERNEL, "%02d%02d%02d.%c%c\n",
			      version->hardware,
			      le16_to_cpu(version->major),
			      version->minor,
			      version->letter[0],
			      version->letter[1]);
}

static int rave_sp_rdu1_get_status(struct rave_sp *sp,
				   struct rave_sp_status *status)
{
	u8 cmd[] = {
		[0] = RAVE_SP_CMD_STATUS,
		[1] = 0
	};

	return rave_sp_exec(sp, cmd, sizeof(cmd), status, sizeof(*status));
}

static int rave_sp_emulated_get_status(struct rave_sp *sp,
				       struct rave_sp_status *status)
{
	u8 cmd[] = {
		[0] = RAVE_SP_CMD_GET_FIRMWARE_VERSION,
		[1] = 0,
	};
	int ret;

	ret = rave_sp_exec(sp, cmd, sizeof(cmd), &status->firmware_version,
			   sizeof(status->firmware_version));
	if (ret)
		return ret;

	cmd[0] = RAVE_SP_CMD_GET_BOOTLOADER_VERSION;
	return rave_sp_exec(sp, cmd, sizeof(cmd), &status->bootloader_version,
			    sizeof(status->bootloader_version));
}

static int rave_sp_get_status(struct rave_sp *sp)
{
	struct device *dev = &sp->serdev->dev;
	struct rave_sp_status status;
	const char *version;
	int ret;

	ret = sp->variant->cmd.get_status(sp, &status);
	if (ret)
		return ret;

	version = devm_rave_sp_version(dev, &status.firmware_version);
	if (!version)
		return -ENOMEM;

	sp->part_number_firmware = version;

	version = devm_rave_sp_version(dev, &status.bootloader_version);
	if (!version)
		return -ENOMEM;

	sp->part_number_bootloader = version;

	return 0;
}

static const struct rave_sp_checksum rave_sp_checksum_8b2c = {
	.length     = 1,
	.subroutine = csum_8b2c,
};

static const struct rave_sp_checksum rave_sp_checksum_ccitt = {
	.length     = 2,
	.subroutine = csum_ccitt,
};

static const struct rave_sp_variant rave_sp_legacy = {
	.checksum = &rave_sp_checksum_ccitt,
	.cmd = {
		.translate = rave_sp_default_cmd_translate,
		.get_status = rave_sp_emulated_get_status,
	},
};

static const struct rave_sp_variant rave_sp_rdu1 = {
	.checksum = &rave_sp_checksum_8b2c,
	.cmd = {
		.translate = rave_sp_rdu1_cmd_translate,
		.get_status = rave_sp_rdu1_get_status,
	},
};

static const struct rave_sp_variant rave_sp_rdu2 = {
	.checksum = &rave_sp_checksum_ccitt,
	.cmd = {
		.translate = rave_sp_rdu2_cmd_translate,
		.get_status = rave_sp_emulated_get_status,
	},
};

static const struct of_device_id rave_sp_dt_ids[] = {
	{ .compatible = "zii,rave-sp-niu",  .data = &rave_sp_legacy },
	{ .compatible = "zii,rave-sp-mezz", .data = &rave_sp_legacy },
	{ .compatible = "zii,rave-sp-esb",  .data = &rave_sp_legacy },
	{ .compatible = "zii,rave-sp-rdu1", .data = &rave_sp_rdu1   },
	{ .compatible = "zii,rave-sp-rdu2", .data = &rave_sp_rdu2   },
	{ /* sentinel */ }
};

static const struct serdev_device_ops rave_sp_serdev_device_ops = {
	.receive_buf  = rave_sp_receive_buf,
	.write_wakeup = serdev_device_write_wakeup,
};

static int rave_sp_probe(struct serdev_device *serdev)
{
	struct device *dev = &serdev->dev;
	const char *unknown = "unknown\n";
	struct rave_sp *sp;
	u32 baud;
	int ret;

	if (of_property_read_u32(dev->of_node, "current-speed", &baud)) {
		dev_err(dev,
			"'current-speed' is not specified in device node\n");
		return -EINVAL;
	}

	sp = devm_kzalloc(dev, sizeof(*sp), GFP_KERNEL);
	if (!sp)
		return -ENOMEM;

	sp->serdev = serdev;
	dev_set_drvdata(dev, sp);

	sp->variant = of_device_get_match_data(dev);
	if (!sp->variant)
		return -ENODEV;

	mutex_init(&sp->bus_lock);
	mutex_init(&sp->reply_lock);
	BLOCKING_INIT_NOTIFIER_HEAD(&sp->event_notifier_list);

	serdev_device_set_client_ops(serdev, &rave_sp_serdev_device_ops);
	ret = devm_serdev_device_open(dev, serdev);
	if (ret)
		return ret;

	serdev_device_set_baudrate(serdev, baud);
	serdev_device_set_flow_control(serdev, false);

	ret = serdev_device_set_parity(serdev, SERDEV_PARITY_NONE);
	if (ret) {
		dev_err(dev, "Failed to set parity\n");
		return ret;
	}

	ret = rave_sp_get_status(sp);
	if (ret) {
		dev_warn(dev, "Failed to get firmware status: %d\n", ret);
		sp->part_number_firmware   = unknown;
		sp->part_number_bootloader = unknown;
	}

	/*
	 * Those strings already have a \n embedded, so there's no
	 * need to have one in format string.
	 */
	dev_info(dev, "Firmware version: %s",   sp->part_number_firmware);
	dev_info(dev, "Bootloader version: %s", sp->part_number_bootloader);

	return devm_of_platform_populate(dev);
}

MODULE_DEVICE_TABLE(of, rave_sp_dt_ids);

static struct serdev_device_driver rave_sp_drv = {
	.probe			= rave_sp_probe,
	.driver = {
		.name		= "rave-sp",
		.of_match_table	= rave_sp_dt_ids,
	},
};
module_serdev_device_driver(rave_sp_drv);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Andrey Vostrikov <andrey.vostrikov@cogentembedded.com>");
MODULE_AUTHOR("Nikita Yushchenko <nikita.yoush@cogentembedded.com>");
MODULE_AUTHOR("Andrey Smirnov <andrew.smirnov@gmail.com>");
MODULE_DESCRIPTION("RAVE SP core driver");
