/*
 *
 * FIXME: Properly make this race free with refcounting etc...
 *
 * FIXME: LOCKING !!!
 */

#include <linux/init.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/mutex.h>

#include <asm/prom.h>
#include <asm/pmac_pfunc.h>

/* Debug */
#define LOG_PARSE(fmt...)
#define LOG_ERROR(fmt...)	printk(fmt)
#define LOG_BLOB(t,b,c)

#undef DEBUG
#ifdef DEBUG
#define DBG(fmt...)		printk(fmt)
#else
#define DBG(fmt...)
#endif

/* Command numbers */
#define PMF_CMD_LIST			0
#define PMF_CMD_WRITE_GPIO		1
#define PMF_CMD_READ_GPIO		2
#define PMF_CMD_WRITE_REG32		3
#define PMF_CMD_READ_REG32		4
#define PMF_CMD_WRITE_REG16		5
#define PMF_CMD_READ_REG16		6
#define PMF_CMD_WRITE_REG8		7
#define PMF_CMD_READ_REG8		8
#define PMF_CMD_DELAY			9
#define PMF_CMD_WAIT_REG32		10
#define PMF_CMD_WAIT_REG16		11
#define PMF_CMD_WAIT_REG8		12
#define PMF_CMD_READ_I2C		13
#define PMF_CMD_WRITE_I2C		14
#define PMF_CMD_RMW_I2C			15
#define PMF_CMD_GEN_I2C			16
#define PMF_CMD_SHIFT_BYTES_RIGHT	17
#define PMF_CMD_SHIFT_BYTES_LEFT	18
#define PMF_CMD_READ_CFG		19
#define PMF_CMD_WRITE_CFG		20
#define PMF_CMD_RMW_CFG			21
#define PMF_CMD_READ_I2C_SUBADDR	22
#define PMF_CMD_WRITE_I2C_SUBADDR	23
#define PMF_CMD_SET_I2C_MODE		24
#define PMF_CMD_RMW_I2C_SUBADDR		25
#define PMF_CMD_READ_REG32_MASK_SHR_XOR	26
#define PMF_CMD_READ_REG16_MASK_SHR_XOR	27
#define PMF_CMD_READ_REG8_MASK_SHR_XOR	28
#define PMF_CMD_WRITE_REG32_SHL_MASK	29
#define PMF_CMD_WRITE_REG16_SHL_MASK	30
#define PMF_CMD_WRITE_REG8_SHL_MASK	31
#define PMF_CMD_MASK_AND_COMPARE	32
#define PMF_CMD_COUNT			33

/* This structure holds the state of the parser while walking through
 * a function definition
 */
struct pmf_cmd {
	const void		*cmdptr;
	const void		*cmdend;
	struct pmf_function	*func;
	void			*instdata;
	struct pmf_args		*args;
	int			error;
};

#if 0
/* Debug output */
static void print_blob(const char *title, const void *blob, int bytes)
{
	printk("%s", title);
	while(bytes--) {
		printk("%02x ", *((u8 *)blob));
		blob += 1;
	}
	printk("\n");
}
#endif

/*
 * Parser helpers
 */

static u32 pmf_next32(struct pmf_cmd *cmd)
{
	u32 value;
	if ((cmd->cmdend - cmd->cmdptr) < 4) {
		cmd->error = 1;
		return 0;
	}
	value = *((u32 *)cmd->cmdptr);
	cmd->cmdptr += 4;
	return value;
}

static const void* pmf_next_blob(struct pmf_cmd *cmd, int count)
{
	const void *value;
	if ((cmd->cmdend - cmd->cmdptr) < count) {
		cmd->error = 1;
		return NULL;
	}
	value = cmd->cmdptr;
	cmd->cmdptr += count;
	return value;
}

/*
 * Individual command parsers
 */

#define PMF_PARSE_CALL(name, cmd, handlers, p...) \
	do { \
		if (cmd->error) \
			return -ENXIO; \
		if (handlers == NULL) \
			return 0; \
		if (handlers->name)				      \
			return handlers->name(cmd->func, cmd->instdata, \
					      cmd->args, p);	      \
		return -1; \
	} while(0) \


static int pmf_parser_write_gpio(struct pmf_cmd *cmd, struct pmf_handlers *h)
{
	u8 value = (u8)pmf_next32(cmd);
	u8 mask = (u8)pmf_next32(cmd);

	LOG_PARSE("pmf: write_gpio(value: %02x, mask: %02x)\n", value, mask);

	PMF_PARSE_CALL(write_gpio, cmd, h, value, mask);
}

static int pmf_parser_read_gpio(struct pmf_cmd *cmd, struct pmf_handlers *h)
{
	u8 mask = (u8)pmf_next32(cmd);
	int rshift = (int)pmf_next32(cmd);
	u8 xor = (u8)pmf_next32(cmd);

	LOG_PARSE("pmf: read_gpio(mask: %02x, rshift: %d, xor: %02x)\n",
		  mask, rshift, xor);

	PMF_PARSE_CALL(read_gpio, cmd, h, mask, rshift, xor);
}

static int pmf_parser_write_reg32(struct pmf_cmd *cmd, struct pmf_handlers *h)
{
	u32 offset = pmf_next32(cmd);
	u32 value = pmf_next32(cmd);
	u32 mask = pmf_next32(cmd);

	LOG_PARSE("pmf: write_reg32(offset: %08x, value: %08x, mask: %08x)\n",
		  offset, value, mask);

	PMF_PARSE_CALL(write_reg32, cmd, h, offset, value, mask);
}

static int pmf_parser_read_reg32(struct pmf_cmd *cmd, struct pmf_handlers *h)
{
	u32 offset = pmf_next32(cmd);

	LOG_PARSE("pmf: read_reg32(offset: %08x)\n", offset);

	PMF_PARSE_CALL(read_reg32, cmd, h, offset);
}


static int pmf_parser_write_reg16(struct pmf_cmd *cmd, struct pmf_handlers *h)
{
	u32 offset = pmf_next32(cmd);
	u16 value = (u16)pmf_next32(cmd);
	u16 mask = (u16)pmf_next32(cmd);

	LOG_PARSE("pmf: write_reg16(offset: %08x, value: %04x, mask: %04x)\n",
		  offset, value, mask);

	PMF_PARSE_CALL(write_reg16, cmd, h, offset, value, mask);
}

static int pmf_parser_read_reg16(struct pmf_cmd *cmd, struct pmf_handlers *h)
{
	u32 offset = pmf_next32(cmd);

	LOG_PARSE("pmf: read_reg16(offset: %08x)\n", offset);

	PMF_PARSE_CALL(read_reg16, cmd, h, offset);
}


static int pmf_parser_write_reg8(struct pmf_cmd *cmd, struct pmf_handlers *h)
{
	u32 offset = pmf_next32(cmd);
	u8 value = (u16)pmf_next32(cmd);
	u8 mask = (u16)pmf_next32(cmd);

	LOG_PARSE("pmf: write_reg8(offset: %08x, value: %02x, mask: %02x)\n",
		  offset, value, mask);

	PMF_PARSE_CALL(write_reg8, cmd, h, offset, value, mask);
}

static int pmf_parser_read_reg8(struct pmf_cmd *cmd, struct pmf_handlers *h)
{
	u32 offset = pmf_next32(cmd);

	LOG_PARSE("pmf: read_reg8(offset: %08x)\n", offset);

	PMF_PARSE_CALL(read_reg8, cmd, h, offset);
}

static int pmf_parser_delay(struct pmf_cmd *cmd, struct pmf_handlers *h)
{
	u32 duration = pmf_next32(cmd);

	LOG_PARSE("pmf: delay(duration: %d us)\n", duration);

	PMF_PARSE_CALL(delay, cmd, h, duration);
}

static int pmf_parser_wait_reg32(struct pmf_cmd *cmd, struct pmf_handlers *h)
{
	u32 offset = pmf_next32(cmd);
	u32 value = pmf_next32(cmd);
	u32 mask = pmf_next32(cmd);

	LOG_PARSE("pmf: wait_reg32(offset: %08x, comp_value: %08x,mask: %08x)\n",
		  offset, value, mask);

	PMF_PARSE_CALL(wait_reg32, cmd, h, offset, value, mask);
}

static int pmf_parser_wait_reg16(struct pmf_cmd *cmd, struct pmf_handlers *h)
{
	u32 offset = pmf_next32(cmd);
	u16 value = (u16)pmf_next32(cmd);
	u16 mask = (u16)pmf_next32(cmd);

	LOG_PARSE("pmf: wait_reg16(offset: %08x, comp_value: %04x,mask: %04x)\n",
		  offset, value, mask);

	PMF_PARSE_CALL(wait_reg16, cmd, h, offset, value, mask);
}

static int pmf_parser_wait_reg8(struct pmf_cmd *cmd, struct pmf_handlers *h)
{
	u32 offset = pmf_next32(cmd);
	u8 value = (u8)pmf_next32(cmd);
	u8 mask = (u8)pmf_next32(cmd);

	LOG_PARSE("pmf: wait_reg8(offset: %08x, comp_value: %02x,mask: %02x)\n",
		  offset, value, mask);

	PMF_PARSE_CALL(wait_reg8, cmd, h, offset, value, mask);
}

static int pmf_parser_read_i2c(struct pmf_cmd *cmd, struct pmf_handlers *h)
{
	u32 bytes = pmf_next32(cmd);

	LOG_PARSE("pmf: read_i2c(bytes: %ud)\n", bytes);

	PMF_PARSE_CALL(read_i2c, cmd, h, bytes);
}

static int pmf_parser_write_i2c(struct pmf_cmd *cmd, struct pmf_handlers *h)
{
	u32 bytes = pmf_next32(cmd);
	const void *blob = pmf_next_blob(cmd, bytes);

	LOG_PARSE("pmf: write_i2c(bytes: %ud) ...\n", bytes);
	LOG_BLOB("pmf:   data: \n", blob, bytes);

	PMF_PARSE_CALL(write_i2c, cmd, h, bytes, blob);
}


static int pmf_parser_rmw_i2c(struct pmf_cmd *cmd, struct pmf_handlers *h)
{
	u32 maskbytes = pmf_next32(cmd);
	u32 valuesbytes = pmf_next32(cmd);
	u32 totalbytes = pmf_next32(cmd);
	const void *maskblob = pmf_next_blob(cmd, maskbytes);
	const void *valuesblob = pmf_next_blob(cmd, valuesbytes);

	LOG_PARSE("pmf: rmw_i2c(maskbytes: %ud, valuebytes: %ud, "
		  "totalbytes: %d) ...\n",
		  maskbytes, valuesbytes, totalbytes);
	LOG_BLOB("pmf:   mask data: \n", maskblob, maskbytes);
	LOG_BLOB("pmf:   values data: \n", valuesblob, valuesbytes);

	PMF_PARSE_CALL(rmw_i2c, cmd, h, maskbytes, valuesbytes, totalbytes,
		       maskblob, valuesblob);
}

static int pmf_parser_read_cfg(struct pmf_cmd *cmd, struct pmf_handlers *h)
{
	u32 offset = pmf_next32(cmd);
	u32 bytes = pmf_next32(cmd);

	LOG_PARSE("pmf: read_cfg(offset: %x, bytes: %ud)\n", offset, bytes);

	PMF_PARSE_CALL(read_cfg, cmd, h, offset, bytes);
}


static int pmf_parser_write_cfg(struct pmf_cmd *cmd, struct pmf_handlers *h)
{
	u32 offset = pmf_next32(cmd);
	u32 bytes = pmf_next32(cmd);
	const void *blob = pmf_next_blob(cmd, bytes);

	LOG_PARSE("pmf: write_cfg(offset: %x, bytes: %ud)\n", offset, bytes);
	LOG_BLOB("pmf:   data: \n", blob, bytes);

	PMF_PARSE_CALL(write_cfg, cmd, h, offset, bytes, blob);
}

static int pmf_parser_rmw_cfg(struct pmf_cmd *cmd, struct pmf_handlers *h)
{
	u32 offset = pmf_next32(cmd);
	u32 maskbytes = pmf_next32(cmd);
	u32 valuesbytes = pmf_next32(cmd);
	u32 totalbytes = pmf_next32(cmd);
	const void *maskblob = pmf_next_blob(cmd, maskbytes);
	const void *valuesblob = pmf_next_blob(cmd, valuesbytes);

	LOG_PARSE("pmf: rmw_cfg(maskbytes: %ud, valuebytes: %ud,"
		  " totalbytes: %d) ...\n",
		  maskbytes, valuesbytes, totalbytes);
	LOG_BLOB("pmf:   mask data: \n", maskblob, maskbytes);
	LOG_BLOB("pmf:   values data: \n", valuesblob, valuesbytes);

	PMF_PARSE_CALL(rmw_cfg, cmd, h, offset, maskbytes, valuesbytes,
		       totalbytes, maskblob, valuesblob);
}


static int pmf_parser_read_i2c_sub(struct pmf_cmd *cmd, struct pmf_handlers *h)
{
	u8 subaddr = (u8)pmf_next32(cmd);
	u32 bytes = pmf_next32(cmd);

	LOG_PARSE("pmf: read_i2c_sub(subaddr: %x, bytes: %ud)\n",
		  subaddr, bytes);

	PMF_PARSE_CALL(read_i2c_sub, cmd, h, subaddr, bytes);
}

static int pmf_parser_write_i2c_sub(struct pmf_cmd *cmd, struct pmf_handlers *h)
{
	u8 subaddr = (u8)pmf_next32(cmd);
	u32 bytes = pmf_next32(cmd);
	const void *blob = pmf_next_blob(cmd, bytes);

	LOG_PARSE("pmf: write_i2c_sub(subaddr: %x, bytes: %ud) ...\n",
		  subaddr, bytes);
	LOG_BLOB("pmf:   data: \n", blob, bytes);

	PMF_PARSE_CALL(write_i2c_sub, cmd, h, subaddr, bytes, blob);
}

static int pmf_parser_set_i2c_mode(struct pmf_cmd *cmd, struct pmf_handlers *h)
{
	u32 mode = pmf_next32(cmd);

	LOG_PARSE("pmf: set_i2c_mode(mode: %d)\n", mode);

	PMF_PARSE_CALL(set_i2c_mode, cmd, h, mode);
}


static int pmf_parser_rmw_i2c_sub(struct pmf_cmd *cmd, struct pmf_handlers *h)
{
	u8 subaddr = (u8)pmf_next32(cmd);
	u32 maskbytes = pmf_next32(cmd);
	u32 valuesbytes = pmf_next32(cmd);
	u32 totalbytes = pmf_next32(cmd);
	const void *maskblob = pmf_next_blob(cmd, maskbytes);
	const void *valuesblob = pmf_next_blob(cmd, valuesbytes);

	LOG_PARSE("pmf: rmw_i2c_sub(subaddr: %x, maskbytes: %ud, valuebytes: %ud"
		  ", totalbytes: %d) ...\n",
		  subaddr, maskbytes, valuesbytes, totalbytes);
	LOG_BLOB("pmf:   mask data: \n", maskblob, maskbytes);
	LOG_BLOB("pmf:   values data: \n", valuesblob, valuesbytes);

	PMF_PARSE_CALL(rmw_i2c_sub, cmd, h, subaddr, maskbytes, valuesbytes,
		       totalbytes, maskblob, valuesblob);
}

static int pmf_parser_read_reg32_msrx(struct pmf_cmd *cmd,
				      struct pmf_handlers *h)
{
	u32 offset = pmf_next32(cmd);
	u32 mask = pmf_next32(cmd);
	u32 shift = pmf_next32(cmd);
	u32 xor = pmf_next32(cmd);

	LOG_PARSE("pmf: read_reg32_msrx(offset: %x, mask: %x, shift: %x,"
		  " xor: %x\n", offset, mask, shift, xor);

	PMF_PARSE_CALL(read_reg32_msrx, cmd, h, offset, mask, shift, xor);
}

static int pmf_parser_read_reg16_msrx(struct pmf_cmd *cmd,
				      struct pmf_handlers *h)
{
	u32 offset = pmf_next32(cmd);
	u32 mask = pmf_next32(cmd);
	u32 shift = pmf_next32(cmd);
	u32 xor = pmf_next32(cmd);

	LOG_PARSE("pmf: read_reg16_msrx(offset: %x, mask: %x, shift: %x,"
		  " xor: %x\n", offset, mask, shift, xor);

	PMF_PARSE_CALL(read_reg16_msrx, cmd, h, offset, mask, shift, xor);
}
static int pmf_parser_read_reg8_msrx(struct pmf_cmd *cmd,
				     struct pmf_handlers *h)
{
	u32 offset = pmf_next32(cmd);
	u32 mask = pmf_next32(cmd);
	u32 shift = pmf_next32(cmd);
	u32 xor = pmf_next32(cmd);

	LOG_PARSE("pmf: read_reg8_msrx(offset: %x, mask: %x, shift: %x,"
		  " xor: %x\n", offset, mask, shift, xor);

	PMF_PARSE_CALL(read_reg8_msrx, cmd, h, offset, mask, shift, xor);
}

static int pmf_parser_write_reg32_slm(struct pmf_cmd *cmd,
				      struct pmf_handlers *h)
{
	u32 offset = pmf_next32(cmd);
	u32 shift = pmf_next32(cmd);
	u32 mask = pmf_next32(cmd);

	LOG_PARSE("pmf: write_reg32_slm(offset: %x, shift: %x, mask: %x\n",
		  offset, shift, mask);

	PMF_PARSE_CALL(write_reg32_slm, cmd, h, offset, shift, mask);
}

static int pmf_parser_write_reg16_slm(struct pmf_cmd *cmd,
				      struct pmf_handlers *h)
{
	u32 offset = pmf_next32(cmd);
	u32 shift = pmf_next32(cmd);
	u32 mask = pmf_next32(cmd);

	LOG_PARSE("pmf: write_reg16_slm(offset: %x, shift: %x, mask: %x\n",
		  offset, shift, mask);

	PMF_PARSE_CALL(write_reg16_slm, cmd, h, offset, shift, mask);
}

static int pmf_parser_write_reg8_slm(struct pmf_cmd *cmd,
				     struct pmf_handlers *h)
{
	u32 offset = pmf_next32(cmd);
	u32 shift = pmf_next32(cmd);
	u32 mask = pmf_next32(cmd);

	LOG_PARSE("pmf: write_reg8_slm(offset: %x, shift: %x, mask: %x\n",
		  offset, shift, mask);

	PMF_PARSE_CALL(write_reg8_slm, cmd, h, offset, shift, mask);
}

static int pmf_parser_mask_and_compare(struct pmf_cmd *cmd,
				       struct pmf_handlers *h)
{
	u32 bytes = pmf_next32(cmd);
	const void *maskblob = pmf_next_blob(cmd, bytes);
	const void *valuesblob = pmf_next_blob(cmd, bytes);

	LOG_PARSE("pmf: mask_and_compare(length: %ud ...\n", bytes);
	LOG_BLOB("pmf:   mask data: \n", maskblob, bytes);
	LOG_BLOB("pmf:   values data: \n", valuesblob, bytes);

	PMF_PARSE_CALL(mask_and_compare, cmd, h,
		       bytes, maskblob, valuesblob);
}


typedef int (*pmf_cmd_parser_t)(struct pmf_cmd *cmd, struct pmf_handlers *h);

static pmf_cmd_parser_t pmf_parsers[PMF_CMD_COUNT] =
{
	NULL,
	pmf_parser_write_gpio,
	pmf_parser_read_gpio,
	pmf_parser_write_reg32,
	pmf_parser_read_reg32,
	pmf_parser_write_reg16,
	pmf_parser_read_reg16,
	pmf_parser_write_reg8,
	pmf_parser_read_reg8,
	pmf_parser_delay,
	pmf_parser_wait_reg32,
	pmf_parser_wait_reg16,
	pmf_parser_wait_reg8,
	pmf_parser_read_i2c,
	pmf_parser_write_i2c,
	pmf_parser_rmw_i2c,
	NULL, /* Bogus command */
	NULL, /* Shift bytes right: NYI */
	NULL, /* Shift bytes left: NYI */
	pmf_parser_read_cfg,
	pmf_parser_write_cfg,
	pmf_parser_rmw_cfg,
	pmf_parser_read_i2c_sub,
	pmf_parser_write_i2c_sub,
	pmf_parser_set_i2c_mode,
	pmf_parser_rmw_i2c_sub,
	pmf_parser_read_reg32_msrx,
	pmf_parser_read_reg16_msrx,
	pmf_parser_read_reg8_msrx,
	pmf_parser_write_reg32_slm,
	pmf_parser_write_reg16_slm,
	pmf_parser_write_reg8_slm,
	pmf_parser_mask_and_compare,
};

struct pmf_device {
	struct list_head	link;
	struct device_node	*node;
	struct pmf_handlers	*handlers;
	struct list_head	functions;
	struct kref		ref;
};

static LIST_HEAD(pmf_devices);
static DEFINE_SPINLOCK(pmf_lock);
static DEFINE_MUTEX(pmf_irq_mutex);

static void pmf_release_device(struct kref *kref)
{
	struct pmf_device *dev = container_of(kref, struct pmf_device, ref);
	kfree(dev);
}

static inline void pmf_put_device(struct pmf_device *dev)
{
	kref_put(&dev->ref, pmf_release_device);
}

static inline struct pmf_device *pmf_get_device(struct pmf_device *dev)
{
	kref_get(&dev->ref);
	return dev;
}

static inline struct pmf_device *pmf_find_device(struct device_node *np)
{
	struct pmf_device *dev;

	list_for_each_entry(dev, &pmf_devices, link) {
		if (dev->node == np)
			return pmf_get_device(dev);
	}
	return NULL;
}

static int pmf_parse_one(struct pmf_function *func,
			 struct pmf_handlers *handlers,
			 void *instdata, struct pmf_args *args)
{
	struct pmf_cmd cmd;
	u32 ccode;
	int count, rc;

	cmd.cmdptr		= func->data;
	cmd.cmdend		= func->data + func->length;
	cmd.func       		= func;
	cmd.instdata		= instdata;
	cmd.args		= args;
	cmd.error		= 0;

	LOG_PARSE("pmf: func %s, %d bytes, %s...\n",
		  func->name, func->length,
		  handlers ? "executing" : "parsing");

	/* One subcommand to parse for now */
	count = 1;

	while(count-- && cmd.cmdptr < cmd.cmdend) {
		/* Get opcode */
		ccode = pmf_next32(&cmd);
		/* Check if we are hitting a command list, fetch new count */
		if (ccode == 0) {
			count = pmf_next32(&cmd) - 1;
			ccode = pmf_next32(&cmd);
		}
		if (cmd.error) {
			LOG_ERROR("pmf: parse error, not enough data\n");
			return -ENXIO;
		}
		if (ccode >= PMF_CMD_COUNT) {
			LOG_ERROR("pmf: command code %d unknown !\n", ccode);
			return -ENXIO;
		}
		if (pmf_parsers[ccode] == NULL) {
			LOG_ERROR("pmf: no parser for command %d !\n", ccode);
			return -ENXIO;
		}
		rc = pmf_parsers[ccode](&cmd, handlers);
		if (rc != 0) {
			LOG_ERROR("pmf: parser for command %d returned"
				  " error %d\n", ccode, rc);
			return rc;
		}
	}

	/* We are doing an initial parse pass, we need to adjust the size */
	if (handlers == NULL)
		func->length = cmd.cmdptr - func->data;

	return 0;
}

static int pmf_add_function_prop(struct pmf_device *dev, void *driverdata,
				 const char *name, u32 *data,
				 unsigned int length)
{
	int count = 0;
	struct pmf_function *func = NULL;

	DBG("pmf: Adding functions for platform-do-%s\n", name);

	while (length >= 12) {
		/* Allocate a structure */
		func = kzalloc(sizeof(struct pmf_function), GFP_KERNEL);
		if (func == NULL)
			goto bail;
		kref_init(&func->ref);
		INIT_LIST_HEAD(&func->irq_clients);
		func->node = dev->node;
		func->driver_data = driverdata;
		func->name = name;
		func->phandle = data[0];
		func->flags = data[1];
		data += 2;
		length -= 8;
		func->data = data;
		func->length = length;
		func->dev = dev;
		DBG("pmf: idx %d: flags=%08x, phandle=%08x "
		    " %d bytes remaining, parsing...\n",
		    count+1, func->flags, func->phandle, length);
		if (pmf_parse_one(func, NULL, NULL, NULL)) {
			kfree(func);
			goto bail;
		}
		length -= func->length;
		data = (u32 *)(((u8 *)data) + func->length);
		list_add(&func->link, &dev->functions);
		pmf_get_device(dev);
		count++;
	}
 bail:
	DBG("pmf: Added %d functions\n", count);

	return count;
}

static int pmf_add_functions(struct pmf_device *dev, void *driverdata)
{
	struct property *pp;
#define PP_PREFIX "platform-do-"
	const int plen = strlen(PP_PREFIX);
	int count = 0;

	for (pp = dev->node->properties; pp != 0; pp = pp->next) {
		const char *name;
		if (strncmp(pp->name, PP_PREFIX, plen) != 0)
			continue;
		name = pp->name + plen;
		if (strlen(name) && pp->length >= 12)
			count += pmf_add_function_prop(dev, driverdata, name,
						       pp->value, pp->length);
	}
	return count;
}


int pmf_register_driver(struct device_node *np,
			struct pmf_handlers *handlers,
			void *driverdata)
{
	struct pmf_device *dev;
	unsigned long flags;
	int rc = 0;

	if (handlers == NULL)
		return -EINVAL;

	DBG("pmf: registering driver for node %s\n", np->full_name);

	spin_lock_irqsave(&pmf_lock, flags);
	dev = pmf_find_device(np);
	spin_unlock_irqrestore(&pmf_lock, flags);
	if (dev != NULL) {
		DBG("pmf: already there !\n");
		pmf_put_device(dev);
		return -EBUSY;
	}

	dev = kzalloc(sizeof(struct pmf_device), GFP_KERNEL);
	if (dev == NULL) {
		DBG("pmf: no memory !\n");
		return -ENOMEM;
	}
	kref_init(&dev->ref);
	dev->node = of_node_get(np);
	dev->handlers = handlers;
	INIT_LIST_HEAD(&dev->functions);

	rc = pmf_add_functions(dev, driverdata);
	if (rc == 0) {
		DBG("pmf: no functions, disposing.. \n");
		of_node_put(np);
		kfree(dev);
		return -ENODEV;
	}

	spin_lock_irqsave(&pmf_lock, flags);
	list_add(&dev->link, &pmf_devices);
	spin_unlock_irqrestore(&pmf_lock, flags);

	return 0;
}
EXPORT_SYMBOL_GPL(pmf_register_driver);

struct pmf_function *pmf_get_function(struct pmf_function *func)
{
	if (!try_module_get(func->dev->handlers->owner))
		return NULL;
	kref_get(&func->ref);
	return func;
}
EXPORT_SYMBOL_GPL(pmf_get_function);

static void pmf_release_function(struct kref *kref)
{
	struct pmf_function *func =
		container_of(kref, struct pmf_function, ref);
	pmf_put_device(func->dev);
	kfree(func);
}

static inline void __pmf_put_function(struct pmf_function *func)
{
	kref_put(&func->ref, pmf_release_function);
}

void pmf_put_function(struct pmf_function *func)
{
	if (func == NULL)
		return;
	module_put(func->dev->handlers->owner);
	__pmf_put_function(func);
}
EXPORT_SYMBOL_GPL(pmf_put_function);

void pmf_unregister_driver(struct device_node *np)
{
	struct pmf_device *dev;
	unsigned long flags;

	DBG("pmf: unregistering driver for node %s\n", np->full_name);

	spin_lock_irqsave(&pmf_lock, flags);
	dev = pmf_find_device(np);
	if (dev == NULL) {
		DBG("pmf: not such driver !\n");
		spin_unlock_irqrestore(&pmf_lock, flags);
		return;
	}
	list_del(&dev->link);

	while(!list_empty(&dev->functions)) {
		struct pmf_function *func =
			list_entry(dev->functions.next, typeof(*func), link);
		list_del(&func->link);
		__pmf_put_function(func);
	}

	pmf_put_device(dev);
	spin_unlock_irqrestore(&pmf_lock, flags);
}
EXPORT_SYMBOL_GPL(pmf_unregister_driver);

struct pmf_function *__pmf_find_function(struct device_node *target,
					 const char *name, u32 flags)
{
	struct device_node *actor = of_node_get(target);
	struct pmf_device *dev;
	struct pmf_function *func, *result = NULL;
	char fname[64];
	const u32 *prop;
	u32 ph;

	/*
	 * Look for a "platform-*" function reference. If we can't find
	 * one, then we fallback to a direct call attempt
	 */
	snprintf(fname, 63, "platform-%s", name);
	prop = of_get_property(target, fname, NULL);
	if (prop == NULL)
		goto find_it;
	ph = *prop;
	if (ph == 0)
		goto find_it;

	/*
	 * Ok, now try to find the actor. If we can't find it, we fail,
	 * there is no point in falling back there
	 */
	of_node_put(actor);
	actor = of_find_node_by_phandle(ph);
	if (actor == NULL)
		return NULL;
 find_it:
	dev = pmf_find_device(actor);
	if (dev == NULL) {
		result = NULL;
		goto out;
	}

	list_for_each_entry(func, &dev->functions, link) {
		if (name && strcmp(name, func->name))
			continue;
		if (func->phandle && target->phandle != func->phandle)
			continue;
		if ((func->flags & flags) == 0)
			continue;
		result = func;
		break;
	}
	pmf_put_device(dev);
out:
	of_node_put(actor);
	return result;
}


int pmf_register_irq_client(struct device_node *target,
			    const char *name,
			    struct pmf_irq_client *client)
{
	struct pmf_function *func;
	unsigned long flags;

	spin_lock_irqsave(&pmf_lock, flags);
	func = __pmf_find_function(target, name, PMF_FLAGS_INT_GEN);
	if (func)
		func = pmf_get_function(func);
	spin_unlock_irqrestore(&pmf_lock, flags);
	if (func == NULL)
		return -ENODEV;

	/* guard against manipulations of list */
	mutex_lock(&pmf_irq_mutex);
	if (list_empty(&func->irq_clients))
		func->dev->handlers->irq_enable(func);

	/* guard against pmf_do_irq while changing list */
	spin_lock_irqsave(&pmf_lock, flags);
	list_add(&client->link, &func->irq_clients);
	spin_unlock_irqrestore(&pmf_lock, flags);

	client->func = func;
	mutex_unlock(&pmf_irq_mutex);

	return 0;
}
EXPORT_SYMBOL_GPL(pmf_register_irq_client);

void pmf_unregister_irq_client(struct pmf_irq_client *client)
{
	struct pmf_function *func = client->func;
	unsigned long flags;

	BUG_ON(func == NULL);

	/* guard against manipulations of list */
	mutex_lock(&pmf_irq_mutex);
	client->func = NULL;

	/* guard against pmf_do_irq while changing list */
	spin_lock_irqsave(&pmf_lock, flags);
	list_del(&client->link);
	spin_unlock_irqrestore(&pmf_lock, flags);

	if (list_empty(&func->irq_clients))
		func->dev->handlers->irq_disable(func);
	mutex_unlock(&pmf_irq_mutex);
	pmf_put_function(func);
}
EXPORT_SYMBOL_GPL(pmf_unregister_irq_client);


void pmf_do_irq(struct pmf_function *func)
{
	unsigned long flags;
	struct pmf_irq_client *client;

	/* For now, using a spinlock over the whole function. Can be made
	 * to drop the lock using 2 lists if necessary
	 */
	spin_lock_irqsave(&pmf_lock, flags);
	list_for_each_entry(client, &func->irq_clients, link) {
		if (!try_module_get(client->owner))
			continue;
		client->handler(client->data);
		module_put(client->owner);
	}
	spin_unlock_irqrestore(&pmf_lock, flags);
}
EXPORT_SYMBOL_GPL(pmf_do_irq);


int pmf_call_one(struct pmf_function *func, struct pmf_args *args)
{
	struct pmf_device *dev = func->dev;
	void *instdata = NULL;
	int rc = 0;

	DBG(" ** pmf_call_one(%s/%s) **\n", dev->node->full_name, func->name);

	if (dev->handlers->begin)
		instdata = dev->handlers->begin(func, args);
	rc = pmf_parse_one(func, dev->handlers, instdata, args);
	if (dev->handlers->end)
		dev->handlers->end(func, instdata);

	return rc;
}
EXPORT_SYMBOL_GPL(pmf_call_one);

int pmf_do_functions(struct device_node *np, const char *name,
		     u32 phandle, u32 fflags, struct pmf_args *args)
{
	struct pmf_device *dev;
	struct pmf_function *func, *tmp;
	unsigned long flags;
	int rc = -ENODEV;

	spin_lock_irqsave(&pmf_lock, flags);

	dev = pmf_find_device(np);
	if (dev == NULL) {
		spin_unlock_irqrestore(&pmf_lock, flags);
		return -ENODEV;
	}
	list_for_each_entry_safe(func, tmp, &dev->functions, link) {
		if (name && strcmp(name, func->name))
			continue;
		if (phandle && func->phandle && phandle != func->phandle)
			continue;
		if ((func->flags & fflags) == 0)
			continue;
		if (pmf_get_function(func) == NULL)
			continue;
		spin_unlock_irqrestore(&pmf_lock, flags);
		rc = pmf_call_one(func, args);
		pmf_put_function(func);
		spin_lock_irqsave(&pmf_lock, flags);
	}
	pmf_put_device(dev);
	spin_unlock_irqrestore(&pmf_lock, flags);

	return rc;
}
EXPORT_SYMBOL_GPL(pmf_do_functions);


struct pmf_function *pmf_find_function(struct device_node *target,
				       const char *name)
{
	struct pmf_function *func;
	unsigned long flags;

	spin_lock_irqsave(&pmf_lock, flags);
	func = __pmf_find_function(target, name, PMF_FLAGS_ON_DEMAND);
	if (func)
		func = pmf_get_function(func);
	spin_unlock_irqrestore(&pmf_lock, flags);
	return func;
}
EXPORT_SYMBOL_GPL(pmf_find_function);

int pmf_call_function(struct device_node *target, const char *name,
		      struct pmf_args *args)
{
	struct pmf_function *func = pmf_find_function(target, name);
	int rc;

	if (func == NULL)
		return -ENODEV;

	rc = pmf_call_one(func, args);
	pmf_put_function(func);
	return rc;
}
EXPORT_SYMBOL_GPL(pmf_call_function);

