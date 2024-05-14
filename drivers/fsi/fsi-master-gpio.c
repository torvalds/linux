// SPDX-License-Identifier: GPL-2.0-only
/*
 * A FSI master controller, using a simple GPIO bit-banging interface
 */

#include <linux/crc4.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/fsi.h>
#include <linux/gpio/consumer.h>
#include <linux/io.h>
#include <linux/irqflags.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "fsi-master.h"

#define	FSI_GPIO_STD_DLY	1	/* Standard pin delay in nS */
#define LAST_ADDR_INVALID		0x1

struct fsi_master_gpio {
	struct fsi_master	master;
	struct device		*dev;
	struct mutex		cmd_lock;	/* mutex for command ordering */
	struct gpio_desc	*gpio_clk;
	struct gpio_desc	*gpio_data;
	struct gpio_desc	*gpio_trans;	/* Voltage translator */
	struct gpio_desc	*gpio_enable;	/* FSI enable */
	struct gpio_desc	*gpio_mux;	/* Mux control */
	bool			external_mode;
	bool			no_delays;
	uint32_t		last_addr;
	uint8_t			t_send_delay;
	uint8_t			t_echo_delay;
};

#define CREATE_TRACE_POINTS
#include <trace/events/fsi_master_gpio.h>

#define to_fsi_master_gpio(m) container_of(m, struct fsi_master_gpio, master)

struct fsi_gpio_msg {
	uint64_t	msg;
	uint8_t		bits;
};

static void clock_toggle(struct fsi_master_gpio *master, int count)
{
	int i;

	for (i = 0; i < count; i++) {
		if (!master->no_delays)
			ndelay(FSI_GPIO_STD_DLY);
		gpiod_set_value(master->gpio_clk, 0);
		if (!master->no_delays)
			ndelay(FSI_GPIO_STD_DLY);
		gpiod_set_value(master->gpio_clk, 1);
	}
}

static int sda_clock_in(struct fsi_master_gpio *master)
{
	int in;

	if (!master->no_delays)
		ndelay(FSI_GPIO_STD_DLY);
	gpiod_set_value(master->gpio_clk, 0);

	/* Dummy read to feed the synchronizers */
	gpiod_get_value(master->gpio_data);

	/* Actual data read */
	in = gpiod_get_value(master->gpio_data);
	if (!master->no_delays)
		ndelay(FSI_GPIO_STD_DLY);
	gpiod_set_value(master->gpio_clk, 1);
	return in ? 1 : 0;
}

static void sda_out(struct fsi_master_gpio *master, int value)
{
	gpiod_set_value(master->gpio_data, value);
}

static void set_sda_input(struct fsi_master_gpio *master)
{
	gpiod_direction_input(master->gpio_data);
	gpiod_set_value(master->gpio_trans, 0);
}

static void set_sda_output(struct fsi_master_gpio *master, int value)
{
	gpiod_set_value(master->gpio_trans, 1);
	gpiod_direction_output(master->gpio_data, value);
}

static void clock_zeros(struct fsi_master_gpio *master, int count)
{
	trace_fsi_master_gpio_clock_zeros(master, count);
	set_sda_output(master, 1);
	clock_toggle(master, count);
}

static void echo_delay(struct fsi_master_gpio *master)
{
	clock_zeros(master, master->t_echo_delay);
}


static void serial_in(struct fsi_master_gpio *master, struct fsi_gpio_msg *msg,
			uint8_t num_bits)
{
	uint8_t bit, in_bit;

	set_sda_input(master);

	for (bit = 0; bit < num_bits; bit++) {
		in_bit = sda_clock_in(master);
		msg->msg <<= 1;
		msg->msg |= ~in_bit & 0x1;	/* Data is active low */
	}
	msg->bits += num_bits;

	trace_fsi_master_gpio_in(master, num_bits, msg->msg);
}

static void serial_out(struct fsi_master_gpio *master,
			const struct fsi_gpio_msg *cmd)
{
	uint8_t bit;
	uint64_t msg = ~cmd->msg;	/* Data is active low */
	uint64_t sda_mask = 0x1ULL << (cmd->bits - 1);
	uint64_t last_bit = ~0;
	int next_bit;

	trace_fsi_master_gpio_out(master, cmd->bits, cmd->msg);

	if (!cmd->bits) {
		dev_warn(master->dev, "trying to output 0 bits\n");
		return;
	}
	set_sda_output(master, 0);

	/* Send the start bit */
	sda_out(master, 0);
	clock_toggle(master, 1);

	/* Send the message */
	for (bit = 0; bit < cmd->bits; bit++) {
		next_bit = (msg & sda_mask) >> (cmd->bits - 1);
		if (last_bit ^ next_bit) {
			sda_out(master, next_bit);
			last_bit = next_bit;
		}
		clock_toggle(master, 1);
		msg <<= 1;
	}
}

static void msg_push_bits(struct fsi_gpio_msg *msg, uint64_t data, int bits)
{
	msg->msg <<= bits;
	msg->msg |= data & ((1ull << bits) - 1);
	msg->bits += bits;
}

static void msg_push_crc(struct fsi_gpio_msg *msg)
{
	uint8_t crc;
	int top;

	top = msg->bits & 0x3;

	/* start bit, and any non-aligned top bits */
	crc = crc4(0, 1 << top | msg->msg >> (msg->bits - top), top + 1);

	/* aligned bits */
	crc = crc4(crc, msg->msg, msg->bits - top);

	msg_push_bits(msg, crc, 4);
}

static bool check_same_address(struct fsi_master_gpio *master, int id,
		uint32_t addr)
{
	/* this will also handle LAST_ADDR_INVALID */
	return master->last_addr == (((id & 0x3) << 21) | (addr & ~0x3));
}

static bool check_relative_address(struct fsi_master_gpio *master, int id,
		uint32_t addr, uint32_t *rel_addrp)
{
	uint32_t last_addr = master->last_addr;
	int32_t rel_addr;

	if (last_addr == LAST_ADDR_INVALID)
		return false;

	/* We may be in 23-bit addressing mode, which uses the id as the
	 * top two address bits. So, if we're referencing a different ID,
	 * use absolute addresses.
	 */
	if (((last_addr >> 21) & 0x3) != id)
		return false;

	/* remove the top two bits from any 23-bit addressing */
	last_addr &= (1 << 21) - 1;

	/* We know that the addresses are limited to 21 bits, so this won't
	 * overflow the signed rel_addr */
	rel_addr = addr - last_addr;
	if (rel_addr > 255 || rel_addr < -256)
		return false;

	*rel_addrp = (uint32_t)rel_addr;

	return true;
}

static void last_address_update(struct fsi_master_gpio *master,
		int id, bool valid, uint32_t addr)
{
	if (!valid)
		master->last_addr = LAST_ADDR_INVALID;
	else
		master->last_addr = ((id & 0x3) << 21) | (addr & ~0x3);
}

/*
 * Encode an Absolute/Relative/Same Address command
 */
static void build_ar_command(struct fsi_master_gpio *master,
		struct fsi_gpio_msg *cmd, uint8_t id,
		uint32_t addr, size_t size, const void *data)
{
	int i, addr_bits, opcode_bits;
	bool write = !!data;
	uint8_t ds, opcode;
	uint32_t rel_addr;

	cmd->bits = 0;
	cmd->msg = 0;

	/* we have 21 bits of address max */
	addr &= ((1 << 21) - 1);

	/* cmd opcodes are variable length - SAME_AR is only two bits */
	opcode_bits = 3;

	if (check_same_address(master, id, addr)) {
		/* we still address the byte offset within the word */
		addr_bits = 2;
		opcode_bits = 2;
		opcode = FSI_CMD_SAME_AR;
		trace_fsi_master_gpio_cmd_same_addr(master);

	} else if (check_relative_address(master, id, addr, &rel_addr)) {
		/* 8 bits plus sign */
		addr_bits = 9;
		addr = rel_addr;
		opcode = FSI_CMD_REL_AR;
		trace_fsi_master_gpio_cmd_rel_addr(master, rel_addr);

	} else {
		addr_bits = 21;
		opcode = FSI_CMD_ABS_AR;
		trace_fsi_master_gpio_cmd_abs_addr(master, addr);
	}

	/*
	 * The read/write size is encoded in the lower bits of the address
	 * (as it must be naturally-aligned), and the following ds bit.
	 *
	 *	size	addr:1	addr:0	ds
	 *	1	x	x	0
	 *	2	x	0	1
	 *	4	0	1	1
	 *
	 */
	ds = size > 1 ? 1 : 0;
	addr &= ~(size - 1);
	if (size == 4)
		addr |= 1;

	msg_push_bits(cmd, id, 2);
	msg_push_bits(cmd, opcode, opcode_bits);
	msg_push_bits(cmd, write ? 0 : 1, 1);
	msg_push_bits(cmd, addr, addr_bits);
	msg_push_bits(cmd, ds, 1);
	for (i = 0; write && i < size; i++)
		msg_push_bits(cmd, ((uint8_t *)data)[i], 8);

	msg_push_crc(cmd);
}

static void build_dpoll_command(struct fsi_gpio_msg *cmd, uint8_t slave_id)
{
	cmd->bits = 0;
	cmd->msg = 0;

	msg_push_bits(cmd, slave_id, 2);
	msg_push_bits(cmd, FSI_CMD_DPOLL, 3);
	msg_push_crc(cmd);
}

static void build_epoll_command(struct fsi_gpio_msg *cmd, uint8_t slave_id)
{
	cmd->bits = 0;
	cmd->msg = 0;

	msg_push_bits(cmd, slave_id, 2);
	msg_push_bits(cmd, FSI_CMD_EPOLL, 3);
	msg_push_crc(cmd);
}

static void build_term_command(struct fsi_gpio_msg *cmd, uint8_t slave_id)
{
	cmd->bits = 0;
	cmd->msg = 0;

	msg_push_bits(cmd, slave_id, 2);
	msg_push_bits(cmd, FSI_CMD_TERM, 6);
	msg_push_crc(cmd);
}

/*
 * Note: callers rely specifically on this returning -EAGAIN for
 * a CRC error detected in the response. Use other error code
 * for other situations. It will be converted to something else
 * higher up the stack before it reaches userspace.
 */
static int read_one_response(struct fsi_master_gpio *master,
		uint8_t data_size, struct fsi_gpio_msg *msgp, uint8_t *tagp)
{
	struct fsi_gpio_msg msg;
	unsigned long flags;
	uint32_t crc;
	uint8_t tag;
	int i;

	local_irq_save(flags);

	/* wait for the start bit */
	for (i = 0; i < FSI_MASTER_MTOE_COUNT; i++) {
		msg.bits = 0;
		msg.msg = 0;
		serial_in(master, &msg, 1);
		if (msg.msg)
			break;
	}
	if (i == FSI_MASTER_MTOE_COUNT) {
		dev_dbg(master->dev,
			"Master time out waiting for response\n");
		local_irq_restore(flags);
		return -ETIMEDOUT;
	}

	msg.bits = 0;
	msg.msg = 0;

	/* Read slave ID & response tag */
	serial_in(master, &msg, 4);

	tag = msg.msg & 0x3;

	/* If we have an ACK and we're expecting data, clock the data in too */
	if (tag == FSI_RESP_ACK && data_size)
		serial_in(master, &msg, data_size * 8);

	/* read CRC */
	serial_in(master, &msg, FSI_CRC_SIZE);

	local_irq_restore(flags);

	/* we have a whole message now; check CRC */
	crc = crc4(0, 1, 1);
	crc = crc4(crc, msg.msg, msg.bits);
	if (crc) {
		/* Check if it's all 1's, that probably means the host is off */
		if (((~msg.msg) & ((1ull << msg.bits) - 1)) == 0)
			return -ENODEV;
		dev_dbg(master->dev, "ERR response CRC msg: 0x%016llx (%d bits)\n",
			msg.msg, msg.bits);
		return -EAGAIN;
	}

	if (msgp)
		*msgp = msg;
	if (tagp)
		*tagp = tag;

	return 0;
}

static int issue_term(struct fsi_master_gpio *master, uint8_t slave)
{
	struct fsi_gpio_msg cmd;
	unsigned long flags;
	uint8_t tag;
	int rc;

	build_term_command(&cmd, slave);

	local_irq_save(flags);
	serial_out(master, &cmd);
	echo_delay(master);
	local_irq_restore(flags);

	rc = read_one_response(master, 0, NULL, &tag);
	if (rc < 0) {
		dev_err(master->dev,
				"TERM failed; lost communication with slave\n");
		return -EIO;
	} else if (tag != FSI_RESP_ACK) {
		dev_err(master->dev, "TERM failed; response %d\n", tag);
		return -EIO;
	}

	return 0;
}

static int poll_for_response(struct fsi_master_gpio *master,
		uint8_t slave, uint8_t size, void *data)
{
	struct fsi_gpio_msg response, cmd;
	int busy_count = 0, rc, i;
	unsigned long flags;
	uint8_t tag;
	uint8_t *data_byte = data;
	int crc_err_retries = 0;
retry:
	rc = read_one_response(master, size, &response, &tag);

	/* Handle retries on CRC errors */
	if (rc == -EAGAIN) {
		/* Too many retries ? */
		if (crc_err_retries++ > FSI_CRC_ERR_RETRIES) {
			/*
			 * Pass it up as a -EIO otherwise upper level will retry
			 * the whole command which isn't what we want here.
			 */
			rc = -EIO;
			goto fail;
		}
		dev_dbg(master->dev,
			 "CRC error retry %d\n", crc_err_retries);
		trace_fsi_master_gpio_crc_rsp_error(master);
		build_epoll_command(&cmd, slave);
		local_irq_save(flags);
		clock_zeros(master, FSI_MASTER_EPOLL_CLOCKS);
		serial_out(master, &cmd);
		echo_delay(master);
		local_irq_restore(flags);
		goto retry;
	} else if (rc)
		goto fail;

	switch (tag) {
	case FSI_RESP_ACK:
		if (size && data) {
			uint64_t val = response.msg;
			/* clear crc & mask */
			val >>= 4;
			val &= (1ull << (size * 8)) - 1;

			for (i = 0; i < size; i++) {
				data_byte[size-i-1] = val;
				val >>= 8;
			}
		}
		break;
	case FSI_RESP_BUSY:
		/*
		 * Its necessary to clock slave before issuing
		 * d-poll, not indicated in the hardware protocol
		 * spec. < 20 clocks causes slave to hang, 21 ok.
		 */
		if (busy_count++ < FSI_MASTER_MAX_BUSY) {
			build_dpoll_command(&cmd, slave);
			local_irq_save(flags);
			clock_zeros(master, FSI_MASTER_DPOLL_CLOCKS);
			serial_out(master, &cmd);
			echo_delay(master);
			local_irq_restore(flags);
			goto retry;
		}
		dev_warn(master->dev,
			"ERR slave is stuck in busy state, issuing TERM\n");
		local_irq_save(flags);
		clock_zeros(master, FSI_MASTER_DPOLL_CLOCKS);
		local_irq_restore(flags);
		issue_term(master, slave);
		rc = -EIO;
		break;

	case FSI_RESP_ERRA:
		dev_dbg(master->dev, "ERRA received: 0x%x\n", (int)response.msg);
		rc = -EIO;
		break;
	case FSI_RESP_ERRC:
		dev_dbg(master->dev, "ERRC received: 0x%x\n", (int)response.msg);
		trace_fsi_master_gpio_crc_cmd_error(master);
		rc = -EAGAIN;
		break;
	}

	if (busy_count > 0)
		trace_fsi_master_gpio_poll_response_busy(master, busy_count);
 fail:
	/*
	 * tSendDelay clocks, avoids signal reflections when switching
	 * from receive of response back to send of data.
	 */
	local_irq_save(flags);
	clock_zeros(master, master->t_send_delay);
	local_irq_restore(flags);

	return rc;
}

static int send_request(struct fsi_master_gpio *master,
		struct fsi_gpio_msg *cmd)
{
	unsigned long flags;

	if (master->external_mode)
		return -EBUSY;

	local_irq_save(flags);
	serial_out(master, cmd);
	echo_delay(master);
	local_irq_restore(flags);

	return 0;
}

static int fsi_master_gpio_xfer(struct fsi_master_gpio *master, uint8_t slave,
		struct fsi_gpio_msg *cmd, size_t resp_len, void *resp)
{
	int rc = -EAGAIN, retries = 0;

	while ((retries++) < FSI_CRC_ERR_RETRIES) {
		rc = send_request(master, cmd);
		if (rc)
			break;
		rc = poll_for_response(master, slave, resp_len, resp);
		if (rc != -EAGAIN)
			break;
		rc = -EIO;
		dev_warn(master->dev, "ECRC retry %d\n", retries);

		/* Pace it a bit before retry */
		msleep(1);
	}

	return rc;
}

static int fsi_master_gpio_read(struct fsi_master *_master, int link,
		uint8_t id, uint32_t addr, void *val, size_t size)
{
	struct fsi_master_gpio *master = to_fsi_master_gpio(_master);
	struct fsi_gpio_msg cmd;
	int rc;

	if (link != 0)
		return -ENODEV;

	mutex_lock(&master->cmd_lock);
	build_ar_command(master, &cmd, id, addr, size, NULL);
	rc = fsi_master_gpio_xfer(master, id, &cmd, size, val);
	last_address_update(master, id, rc == 0, addr);
	mutex_unlock(&master->cmd_lock);

	return rc;
}

static int fsi_master_gpio_write(struct fsi_master *_master, int link,
		uint8_t id, uint32_t addr, const void *val, size_t size)
{
	struct fsi_master_gpio *master = to_fsi_master_gpio(_master);
	struct fsi_gpio_msg cmd;
	int rc;

	if (link != 0)
		return -ENODEV;

	mutex_lock(&master->cmd_lock);
	build_ar_command(master, &cmd, id, addr, size, val);
	rc = fsi_master_gpio_xfer(master, id, &cmd, 0, NULL);
	last_address_update(master, id, rc == 0, addr);
	mutex_unlock(&master->cmd_lock);

	return rc;
}

static int fsi_master_gpio_term(struct fsi_master *_master,
		int link, uint8_t id)
{
	struct fsi_master_gpio *master = to_fsi_master_gpio(_master);
	struct fsi_gpio_msg cmd;
	int rc;

	if (link != 0)
		return -ENODEV;

	mutex_lock(&master->cmd_lock);
	build_term_command(&cmd, id);
	rc = fsi_master_gpio_xfer(master, id, &cmd, 0, NULL);
	last_address_update(master, id, false, 0);
	mutex_unlock(&master->cmd_lock);

	return rc;
}

static int fsi_master_gpio_break(struct fsi_master *_master, int link)
{
	struct fsi_master_gpio *master = to_fsi_master_gpio(_master);
	unsigned long flags;

	if (link != 0)
		return -ENODEV;

	trace_fsi_master_gpio_break(master);

	mutex_lock(&master->cmd_lock);
	if (master->external_mode) {
		mutex_unlock(&master->cmd_lock);
		return -EBUSY;
	}

	local_irq_save(flags);

	set_sda_output(master, 1);
	sda_out(master, 1);
	clock_toggle(master, FSI_PRE_BREAK_CLOCKS);
	sda_out(master, 0);
	clock_toggle(master, FSI_BREAK_CLOCKS);
	echo_delay(master);
	sda_out(master, 1);
	clock_toggle(master, FSI_POST_BREAK_CLOCKS);

	local_irq_restore(flags);

	last_address_update(master, 0, false, 0);
	mutex_unlock(&master->cmd_lock);

	/* Wait for logic reset to take effect */
	udelay(200);

	return 0;
}

static void fsi_master_gpio_init(struct fsi_master_gpio *master)
{
	unsigned long flags;

	gpiod_direction_output(master->gpio_mux, 1);
	gpiod_direction_output(master->gpio_trans, 1);
	gpiod_direction_output(master->gpio_enable, 1);
	gpiod_direction_output(master->gpio_clk, 1);
	gpiod_direction_output(master->gpio_data, 1);

	/* todo: evaluate if clocks can be reduced */
	local_irq_save(flags);
	clock_zeros(master, FSI_INIT_CLOCKS);
	local_irq_restore(flags);
}

static void fsi_master_gpio_init_external(struct fsi_master_gpio *master)
{
	gpiod_direction_output(master->gpio_mux, 0);
	gpiod_direction_output(master->gpio_trans, 0);
	gpiod_direction_output(master->gpio_enable, 1);
	gpiod_direction_input(master->gpio_clk);
	gpiod_direction_input(master->gpio_data);
}

static int fsi_master_gpio_link_enable(struct fsi_master *_master, int link,
				       bool enable)
{
	struct fsi_master_gpio *master = to_fsi_master_gpio(_master);
	int rc = -EBUSY;

	if (link != 0)
		return -ENODEV;

	mutex_lock(&master->cmd_lock);
	if (!master->external_mode) {
		gpiod_set_value(master->gpio_enable, enable ? 1 : 0);
		rc = 0;
	}
	mutex_unlock(&master->cmd_lock);

	return rc;
}

static int fsi_master_gpio_link_config(struct fsi_master *_master, int link,
				       u8 t_send_delay, u8 t_echo_delay)
{
	struct fsi_master_gpio *master = to_fsi_master_gpio(_master);

	if (link != 0)
		return -ENODEV;

	mutex_lock(&master->cmd_lock);
	master->t_send_delay = t_send_delay;
	master->t_echo_delay = t_echo_delay;
	mutex_unlock(&master->cmd_lock);

	return 0;
}

static ssize_t external_mode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct fsi_master_gpio *master = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE - 1, "%u\n",
			master->external_mode ? 1 : 0);
}

static ssize_t external_mode_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct fsi_master_gpio *master = dev_get_drvdata(dev);
	unsigned long val;
	bool external_mode;
	int err;

	err = kstrtoul(buf, 0, &val);
	if (err)
		return err;

	external_mode = !!val;

	mutex_lock(&master->cmd_lock);

	if (external_mode == master->external_mode) {
		mutex_unlock(&master->cmd_lock);
		return count;
	}

	master->external_mode = external_mode;
	if (master->external_mode)
		fsi_master_gpio_init_external(master);
	else
		fsi_master_gpio_init(master);

	mutex_unlock(&master->cmd_lock);

	fsi_master_rescan(&master->master);

	return count;
}

static DEVICE_ATTR(external_mode, 0664,
		external_mode_show, external_mode_store);

static void fsi_master_gpio_release(struct device *dev)
{
	struct fsi_master_gpio *master = to_fsi_master_gpio(dev_to_fsi_master(dev));

	of_node_put(dev_of_node(master->dev));

	kfree(master);
}

static int fsi_master_gpio_probe(struct platform_device *pdev)
{
	struct fsi_master_gpio *master;
	struct gpio_desc *gpio;
	int rc;

	master = kzalloc(sizeof(*master), GFP_KERNEL);
	if (!master)
		return -ENOMEM;

	master->dev = &pdev->dev;
	master->master.dev.parent = master->dev;
	master->master.dev.of_node = of_node_get(dev_of_node(master->dev));
	master->master.dev.release = fsi_master_gpio_release;
	master->last_addr = LAST_ADDR_INVALID;

	gpio = devm_gpiod_get(&pdev->dev, "clock", 0);
	if (IS_ERR(gpio)) {
		dev_err(&pdev->dev, "failed to get clock gpio\n");
		rc = PTR_ERR(gpio);
		goto err_free;
	}
	master->gpio_clk = gpio;

	gpio = devm_gpiod_get(&pdev->dev, "data", 0);
	if (IS_ERR(gpio)) {
		dev_err(&pdev->dev, "failed to get data gpio\n");
		rc = PTR_ERR(gpio);
		goto err_free;
	}
	master->gpio_data = gpio;

	/* Optional GPIOs */
	gpio = devm_gpiod_get_optional(&pdev->dev, "trans", 0);
	if (IS_ERR(gpio)) {
		dev_err(&pdev->dev, "failed to get trans gpio\n");
		rc = PTR_ERR(gpio);
		goto err_free;
	}
	master->gpio_trans = gpio;

	gpio = devm_gpiod_get_optional(&pdev->dev, "enable", 0);
	if (IS_ERR(gpio)) {
		dev_err(&pdev->dev, "failed to get enable gpio\n");
		rc = PTR_ERR(gpio);
		goto err_free;
	}
	master->gpio_enable = gpio;

	gpio = devm_gpiod_get_optional(&pdev->dev, "mux", 0);
	if (IS_ERR(gpio)) {
		dev_err(&pdev->dev, "failed to get mux gpio\n");
		rc = PTR_ERR(gpio);
		goto err_free;
	}
	master->gpio_mux = gpio;

	/*
	 * Check if GPIO block is slow enought that no extra delays
	 * are necessary. This improves performance on ast2500 by
	 * an order of magnitude.
	 */
	master->no_delays = device_property_present(&pdev->dev, "no-gpio-delays");

	/* Default FSI command delays */
	master->t_send_delay = FSI_SEND_DELAY_CLOCKS;
	master->t_echo_delay = FSI_ECHO_DELAY_CLOCKS;

	master->master.n_links = 1;
	master->master.flags = FSI_MASTER_FLAG_SWCLOCK;
	master->master.read = fsi_master_gpio_read;
	master->master.write = fsi_master_gpio_write;
	master->master.term = fsi_master_gpio_term;
	master->master.send_break = fsi_master_gpio_break;
	master->master.link_enable = fsi_master_gpio_link_enable;
	master->master.link_config = fsi_master_gpio_link_config;
	platform_set_drvdata(pdev, master);
	mutex_init(&master->cmd_lock);

	fsi_master_gpio_init(master);

	rc = device_create_file(&pdev->dev, &dev_attr_external_mode);
	if (rc)
		goto err_free;

	rc = fsi_master_register(&master->master);
	if (rc) {
		device_remove_file(&pdev->dev, &dev_attr_external_mode);
		put_device(&master->master.dev);
		return rc;
	}
	return 0;
 err_free:
	kfree(master);
	return rc;
}



static int fsi_master_gpio_remove(struct platform_device *pdev)
{
	struct fsi_master_gpio *master = platform_get_drvdata(pdev);

	device_remove_file(&pdev->dev, &dev_attr_external_mode);

	fsi_master_unregister(&master->master);

	return 0;
}

static const struct of_device_id fsi_master_gpio_match[] = {
	{ .compatible = "fsi-master-gpio" },
	{ },
};
MODULE_DEVICE_TABLE(of, fsi_master_gpio_match);

static struct platform_driver fsi_master_gpio_driver = {
	.driver = {
		.name		= "fsi-master-gpio",
		.of_match_table	= fsi_master_gpio_match,
	},
	.probe	= fsi_master_gpio_probe,
	.remove = fsi_master_gpio_remove,
};

module_platform_driver(fsi_master_gpio_driver);
MODULE_LICENSE("GPL");
