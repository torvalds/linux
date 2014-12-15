#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/spi/spi.h>
#include <linux/spi/flash.h>

#include <mach/spi_nor.h>
#include <linux/of.h>
/****************************************************************************/

struct spi_nor {
	struct spi_device	*spi;
	struct mutex		lock;
	struct mtd_info		mtd;
	unsigned		partitioned:1;
	u32				flash_pp_size;
	u8			erase_opcode;
	u8			command[CMD_SIZE + FAST_READ_DUMMY_BYTE];
};

static inline struct spi_nor *mtd_to_spi_nor(struct mtd_info *mtd)
{
	return container_of(mtd, struct spi_nor, mtd);
}

static int read_status(struct spi_nor *spi_nor)
{
	ssize_t retval;
	u8 command = OPCODE_RDSR;
	u8 val;

	retval = spi_write_then_read(spi_nor->spi, &command, 1, &val, 1);

	if (retval < 0) {
		dev_err(&spi_nor->spi->dev, "error %d reading status\n",
				(int) retval);
		return retval;
	}

	return val;
}

static inline int write_enable(struct spi_nor *spi_nor)
{
	u8	command = OPCODE_WREN;
	return spi_write_then_read(spi_nor->spi, &command, 1, NULL, 0);
}

static inline int enable_write_protect(struct spi_nor *spi_nor)
{
	u8	command[2];
	command[0] = OPCODE_WRSR;
	command[1] = (0xf<<2);

	return spi_write_then_read(spi_nor->spi, command, 2, NULL, 0);
}

static inline int disable_write_protect(struct spi_nor *spi_nor)
{
	u8	command[2];
	command[0] = OPCODE_WRSR;
	command[1] = 0;

	return spi_write_then_read(spi_nor->spi, command, 2, NULL, 0);
}

static int wait_till_ready(struct spi_nor *spi_nor, u8 wp_disable_flag)
{
	int count;
	int status;

	/* one chip guarantees max 5 msec wait here after page writes,
	 * but potentially three seconds (!) after page erase.
	 */
	for (count = 0; count < MAX_READY_WAIT_COUNT; count++) {
		if ((status = read_status(spi_nor)) < 0)
			break;
		else {
			if (!(status & SR_WIP)) {
				if (wp_disable_flag) {
					status &= (0xf<<2);
					if (status != 0)
						disable_write_protect(spi_nor);

					status = read_status(spi_nor);
					status &= (0xf<<2);
					if (status != 0)
						return 1;
					else
						return 0;
				}

				return 0;
			}
		}

		/* REVISIT sometimes sleeping would be best */
	}

	return 1;
}


/*
 * Erase one sector of flash memory at offset ``offset'' which is any
 * address within the sector which should be erased.
 *
 * Returns 0 if successful, non-zero otherwise.
 */
static int erase_sector(struct spi_nor *spi_nor, unsigned offset)
{
	pr_debug("%s: %s %dKiB at 0x%08x\n", dev_name(&spi_nor->spi->dev), __func__, spi_nor->mtd.erasesize / 1024, offset);

	/* Wait until finished previous write command. */
	if (wait_till_ready(spi_nor, 1))
		return 1;

	/* Send write enable, then erase commands. */
	write_enable(spi_nor);

	/* Set up command buffer. */
	spi_nor->command[0] = spi_nor->erase_opcode;
	spi_nor->command[1] = offset >> 16;
	spi_nor->command[2] = offset >> 8;
	spi_nor->command[3] = offset;

	spi_write(spi_nor->spi, spi_nor->command, CMD_SIZE);

	return 0;
}

/****************************************************************************/

/*
 * MTD implementation
 */

/*
 * Erase an address range on the flash chip.  The address range may extend
 * one or more erase sectors.  Return an error is there is a problem erasing.
 */
static int spi_nor_erase(struct mtd_info *mtd, struct erase_info *instr)
{
	struct spi_nor *spi_nor = mtd_to_spi_nor(mtd);
	u32 addr,len;


	pr_debug("%s: %s %dKiB at 0x%08x\n",
			dev_name(&spi_nor->spi->dev), __func__,
			spi_nor->mtd.erasesize / 1024, (u32)instr->addr);

	addr = instr->addr;
	len = instr->len;
	/* sanity checks */
	if (instr->addr + instr->len > spi_nor->mtd.size)
		return -EINVAL;
	if ((addr % mtd->erasesize) != 0
			|| (len % mtd->erasesize) != 0) {
		return -EINVAL;
	}

	mutex_lock(&spi_nor->lock);

	/* REVISIT in some cases we could speed up erasing large regions
	 * by using OPCODE_SE instead of OPCODE_BE_4K
	 */

	/* now erase those sectors */
	while (len) {
		if (erase_sector(spi_nor, addr)) {
			instr->state = MTD_ERASE_FAILED;
			mutex_unlock(&spi_nor->lock);
			return -EIO;
		}

		addr += mtd->erasesize;
		len -= mtd->erasesize;
	}

	mutex_unlock(&spi_nor->lock);

	instr->state = MTD_ERASE_DONE;
	mtd_erase_callback(instr);

	return 0;
}

/*
 * Read an address range from the flash chip.  The address range
 * may be any size provided it is within the physical boundaries.
 */
static int spi_nor_read(struct mtd_info *mtd, loff_t from, size_t len,
	size_t *retlen, u_char *buf)
{
	struct spi_nor *spi_nor = mtd_to_spi_nor(mtd);
	struct spi_transfer t[2];
	struct spi_message m;

	pr_debug("%s: %s %s 0x%08x, len %zd\n",
			dev_name(&spi_nor->spi->dev), __func__, "from",
			(u32)from, len);

	/* sanity checks */
	if (!len)
		return 0;

	if (from + len > spi_nor->mtd.size)
		return -EINVAL;

	spi_message_init(&m);
	memset(t, 0, (sizeof t));

	/* NOTE:
	 * OPCODE_FAST_READ (if available) is faster.
	 * Should add 1 byte DUMMY_BYTE.
	 */
	t[0].tx_buf = spi_nor->command;
	t[0].len = CMD_SIZE + FAST_READ_DUMMY_BYTE;
	spi_message_add_tail(&t[0], &m);

	t[1].rx_buf = buf;
	t[1].len = len;
	spi_message_add_tail(&t[1], &m);

	/* Byte count starts at zero. */
	if (retlen)
		*retlen = 0;

	mutex_lock(&spi_nor->lock);

	/* Wait till previous write/erase is done. */
	if (wait_till_ready(spi_nor, 0)) {
		/* REVISIT status return?? */
		mutex_unlock(&spi_nor->lock);
		return 1;
	}

	/* FIXME switch to OPCODE_FAST_READ.  It's required for higher
	 * clocks; and at this writing, every chip this driver handles
	 * supports that opcode.
	 */

	/* Set up the write data buffer. */
	spi_nor->command[0] = OPCODE_READ;
	spi_nor->command[1] = from >> 16;
	spi_nor->command[2] = from >> 8;
	spi_nor->command[3] = from;

	spi_sync(spi_nor->spi, &m);

	*retlen = m.actual_length - CMD_SIZE - FAST_READ_DUMMY_BYTE;

	mutex_unlock(&spi_nor->lock);

	return 0;
}

/*
 * Write an address range to the flash chip.  Data must be written in
 * FLASH_PAGESIZE chunks.  The address range may be any size provided
 * it is within the physical boundaries.
 */
static int spi_nor_write(struct mtd_info *mtd, loff_t to, size_t len,
	size_t *retlen, const u_char *buf)
{
	struct spi_nor *spi_nor = mtd_to_spi_nor(mtd);
	u32 page_offset, page_size, flash_pp_size, addr_to;
	struct spi_transfer t[2];
	struct spi_message m;

	pr_debug("%s: %s %s 0x%08x, len %zd\n",
			dev_name(&spi_nor->spi->dev), __func__, "to",
			(u32)to, len);

	flash_pp_size = spi_nor->flash_pp_size;
	addr_to = to;

	if (retlen)
		*retlen = 0;

	/* sanity checks */
	if (!len)
		return(0);

	if (to + len > spi_nor->mtd.size)
		return -EINVAL;

	spi_message_init(&m);
	memset(t, 0, (sizeof t));

	t[0].tx_buf = spi_nor->command;
	t[0].len = CMD_SIZE;
	spi_message_add_tail(&t[0], &m);

	t[1].tx_buf = buf;
	spi_message_add_tail(&t[1], &m);

	mutex_lock(&spi_nor->lock);

	/* Wait until finished previous write command. */
	if (wait_till_ready(spi_nor, 1)) {
		mutex_unlock(&spi_nor->lock);
		return 1;
	}

	write_enable(spi_nor);

	/* Set up the opcode in the write buffer. */
	spi_nor->command[0] = OPCODE_PP;
	spi_nor->command[1] = to >> 16;
	spi_nor->command[2] = to >> 8;
	spi_nor->command[3] = to;

	/* what page do we start with? */
	page_offset = addr_to % flash_pp_size;

	/* do all the bytes fit onto one page? */
	if (page_offset + len <= flash_pp_size) {
		t[1].len = len;

		spi_sync(spi_nor->spi, &m);

		*retlen = m.actual_length - CMD_SIZE;
	} else {
		u32 i;

		/* the size of data remaining on the first page */
		page_size = flash_pp_size - page_offset;

		t[1].len = page_size;
		spi_sync(spi_nor->spi, &m);

		*retlen = m.actual_length - CMD_SIZE;

		/* write everything in PAGESIZE chunks */
		for (i = page_size; i < len; i += page_size) {
			page_size = len - i;
			if (page_size > flash_pp_size)
				page_size = flash_pp_size;

			/* write the next page to flash */
			spi_nor->command[1] = (to + i) >> 16;
			spi_nor->command[2] = (to + i) >> 8;
			spi_nor->command[3] = (to + i);

			t[1].tx_buf = buf + i;
			t[1].len = page_size;

			wait_till_ready(spi_nor, 1);

			spi_sync(spi_nor->spi, &m);

			if (retlen)
				*retlen += m.actual_length - CMD_SIZE;
		}
	}
	enable_write_protect(spi_nor); // add by LiG
	mutex_unlock(&spi_nor->lock);

	return 0;
}


/****************************************************************************/

/*
 * SPI device driver setup and teardown
 */

struct flash_info {
	char		*name;

	/* JEDEC id zero means "no ID" (most older chips); otherwise it has
	 * a high byte of zero plus three data bytes: the manufacturer id,
	 * then a two byte device id.
	 */
	u32		jedec_id;

	/* The size listed here is what works with OPCODE_BE, which isn't
	 * necessarily called a "sector" by the vendor.
	 */
	unsigned	block_size;
	u16			n_blocks;

	u16		flags;
#define	SECT_4K		0x01		/* OPCODE_BE_4K works uniformly */
#define BLOCK_64K	0x02		/* for jffs2 use */
};


/* NOTE: double check command sets and memory organization when you add
 * more flash chips.  This current list focusses on newer chips, which
 * have been converging on command sets which including JEDEC ID.
 */
static struct flash_info spi_nor_data [] = {

	/* Atmel -- some are (confusingly) marketed as "DataFlash" */
	{ "at25fs010",  0x1f6601, 32 * 1024, 4, SECT_4K, },
	{ "at25fs040",  0x1f6604, 64 * 1024, 8, SECT_4K, },

	{ "at25df041a", 0x1f4401, 64 * 1024, 8, SECT_4K, },
	{ "at25df641",  0x1f4800, 64 * 1024, 128, SECT_4K, },

	{ "at26f004",   0x1f0400, 64 * 1024, 8, SECT_4K, },
	{ "at26df081a", 0x1f4501, 64 * 1024, 16, SECT_4K, },
	{ "at26df161a", 0x1f4601, 64 * 1024, 32, SECT_4K, },
	{ "at26df321",  0x1f4701, 64 * 1024, 64, SECT_4K, },

	/* Spansion -- single (large) sector size only, at least
	 * for the chips listed here (without boot sectors).
	 */
	{ "s25sl004a", 0x010212, 64 * 1024, 8, },
	{ "s25sl008a", 0x010213, 64 * 1024, 16, },
	{ "s25sl016a", 0x010214, 64 * 1024, 32, },
	{ "s25sl032a", 0x010215, 64 * 1024, 64, },
	{ "s25sl064a", 0x010216, 64 * 1024, 128, },
	{ "s25fl032a", 0x010215, 64 * 1024, 64, },

	/* SST -- large erase sizes are "overlays", "sectors" are 4K */
	{ "sst25vf040b", 0xbf258d, 64 * 1024, 8, SECT_4K, },
	{ "sst25vf080b", 0xbf258e, 64 * 1024, 16, SECT_4K, },
	{ "sst25vf016b", 0xbf2541, 64 * 1024, 32, SECT_4K, },
	{ "sst25vf032b", 0xbf254a, 64 * 1024, 64, SECT_4K, },

	/* ST Microelectronics -- newer production may have feature updates */
	{ "m25p05",  0x202010,  32 * 1024, 2, },
	{ "m25p10",  0x202011,  32 * 1024, 4, },
	{ "m25p20",  0x202012,  64 * 1024, 4, },
	{ "m25p40",  0x202013,  64 * 1024, 8, },
	{ "m25p80",  0x202014,  64 * 1024, 16, },
	{ "m25p16",  0x202015,  64 * 1024, 32, },
	{ "m25p32",  0x202016,  64 * 1024, 64, },
	{ "m25p64",  0x202017,  64 * 1024, 128, },
	{ "m25p128", 0x202018, 256 * 1024, 64, },

	{ "m45pe80", 0x204014,  64 * 1024, 16, },
	{ "m45pe16", 0x204015,  64 * 1024, 32, },

	{ "m25pe80", 0x208014,  64 * 1024, 16, },
	{ "m25pe16", 0x208015,  64 * 1024, 32, SECT_4K, },

	/* Winbond -- w25x "blocks" are 64K, "sectors" are 4KiB */
	{ "w25x10", 0xef3011, 64 * 1024, 2, SECT_4K, },
	{ "w25x20", 0xef3012, 64 * 1024, 4, SECT_4K, },
	{ "w25x40", 0xef3013, 64 * 1024, 8, SECT_4K, },
	{ "w25x80", 0xef3014, 64 * 1024, 16, SECT_4K, },
	{ "w25x16", 0xef3015, 64 * 1024, 32, SECT_4K, },
	{ "w25x32", 0xef3016, 64 * 1024, 64, SECT_4K, },
	{ "w25x64", 0xef3017, 64 * 1024, 128, SECT_4K, },
	{ "w25q16", 0xef4015, 64 * 1024, 32, SECT_4K, },
	{ "w25q16", 0xef4016, 64 * 1024, 64, SECT_4K, },
	{ "w25q64", 0xef4017, 64 * 1024, 128, SECT_4K, },

	/*en25f----*/
	{ "en25f10", 0x1c3111, 64 * 1024, 32, SECT_4K, },
	{ "en25f16", 0x1c3115, 64 * 1024, 32, SECT_4K, },
	{ "en25fb32b", 0x1c2016, 64 * 1024, 64, SECT_4K, },
	{ "en25ff32b", 0x1c3116, 64 * 1024, 64, SECT_4K, },

	/*MX25----*/
	{ "mx25l1005c", 0xC22011, 64 * 1024, 2, SECT_4K, },
	{ "kh25l4005a", 0xC22013, 64 * 1024, 4, SECT_4K, },
	{ "mx25l800", 0xC22014, 64 * 1024, 16, SECT_4K, },
	{ "mx25l160", 0xC22015, 64 * 1024, 32, SECT_4K, },
	{ "mx25l1605", 0xC22015, 64 * 1024, 32, SECT_4K,},
	{ "mx25l3205d", 0xC22016, 64 * 1024, 64, BLOCK_64K, },
	{ "mx25l6445e", 0xC22017, 64 * 1024, 128, SECT_4K, },
	{ "mx25l2805d", 0xC22018, 64 * 1024, 256, SECT_4K, },

    /*GigaByte----*/
    { "GD25Q16", 0xC84015, 64 * 1024, 32, SECT_4K, },
    { "GD25Q32", 0xC84016, 64 * 1024, 64, SECT_4K, },
    { "GD25Q40", 0xC84013, 64 * 1024, 8, SECT_4K, },

	 /*PM spi nor*/
	{"pm25lq032c", 0x7f9d46, 64*1024, 64, SECT_4K, },
};

static struct flash_info * jedec_probe(struct spi_device *spi)
{
	int			tmp;
	u8			code = OPCODE_RDID;
	u8			id[3];
	u32			jedec;
	struct flash_info	*info;

	/* JEDEC also defines an optional "extended device information"
	 * string for after vendor-specific data, after the three bytes
	 * we use here.  Supporting some chips might require using it.
	 */
	tmp = spi_write_then_read(spi, &code, 1, id, 3);
	if (tmp < 0) {		
		pr_debug("%s: error %d reading JEDEC ID\n",
			dev_name(&spi->dev), tmp);
		return NULL;
	}	
	jedec = id[0];
	jedec = jedec << 8;
	jedec |= id[1];
	jedec = jedec << 8;
	jedec |= id[2];
	
	for (tmp = 0, info = spi_nor_data;
			tmp < ARRAY_SIZE(spi_nor_data);
			tmp++, info++) {
		if (info->jedec_id == jedec)
			return info;
	}
	dev_err(&spi->dev, "unrecognized JEDEC id %06x\n", jedec);
	return NULL;
}


//boot_flag
#define R_BOOT_DEVICE_FLAG  READ_CBUS_REG(ASSIST_POR_CONFIG)

#ifdef CONFIG_ARCH_MESON8
#define POR_BOOT_VALUE 	((((R_BOOT_DEVICE_FLAG>>9)&1)<<2)|((R_BOOT_DEVICE_FLAG>>6)&3))
#else
#define POR_BOOT_VALUE 	(R_BOOT_DEVICE_FLAG & 7)
#endif

#define POR_NAND_BOOT()	 ((POR_BOOT_VALUE == 7) || (POR_BOOT_VALUE == 6))
#define POR_SPI_BOOT()  		((POR_BOOT_VALUE == 5) || (POR_BOOT_VALUE == 4))
#define POR_EMMC_BOOT()	 (POR_BOOT_VALUE == 3)
#define POR_CARD_BOOT() 	(POR_BOOT_VALUE == 0)

#define SPI_BOOT_FLAG 			0
#define NAND_BOOT_FLAG 		1
#define EMMC_BOOT_FLAG 		2
#define CARD_BOOT_FLAG 		3
#define SPI_NAND_FLAG			4
#define SPI_EMMC_FLAG			5

static int boot_device_flag = -1;
/***
*boot_device_flag = 0 ; indicate spi+nand /spi+emmc boot
*boot_device_flag = 1;  indicate no spi  
***/
static int check_storage_device(void)
{
	int value  = -1;
	value = boot_device_flag;
	
	if((value == -1)||(value == 0)||(value == SPI_NAND_FLAG) ||(value == SPI_EMMC_FLAG) ){
				if((value == 0) ||(value == -1)){ 	
					if(POR_NAND_BOOT()){
						boot_device_flag = -1;
					}else if(POR_EMMC_BOOT()){
						boot_device_flag = -1;
					}else if(POR_SPI_BOOT()){
						boot_device_flag = 0;
					}else if(POR_CARD_BOOT()){
						boot_device_flag = 0;
					}
				}else{
					boot_device_flag = 0;
				}
		}else {
			boot_device_flag = -1;
		}
 	printk("%s : spi boot_device_flag : %d\n",__func__,boot_device_flag);

	if((boot_device_flag == 0)){
		return 0;
	}else{
		boot_device_flag = value;
		return -1;
	}

}

static int  __init get_storage_device(char *str)
{
	int value = -1;
	value = simple_strtoul(str, NULL, 16);
	printk("%s : get storage device: storage %s\n",__func__,str);
	printk("%s : value=%d\n",__func__,value);
	boot_device_flag = value;
	
	return 0;
}

early_param("storage",get_storage_device);

/*
 * board specific setup should have ensured the SPI clock used here
 * matches what the READ command supports, at least until this driver
 * understands FAST_READ (for clocks over 25 MHz).
 */
 static int spi_nor_probe(struct spi_device *spi)
{
	struct flash_platform_data	*data=NULL;
	struct spi_nor			*spi_nor;
	struct flash_info		*info;
	unsigned			i;
	struct mtd_partition *parts;
	int nr;

	/* Platform data helps sort out which chip type we have, as
	 * well as how this board partitions it.  If we don't have
	 * a chip ID, try the JEDEC id commands; they'll work for most
	 * newer chips, even if we don't recognize the particular chip.
	 */
		
#ifdef CONFIG_OF
	int index;
	int val = 0;
	struct mtd_partition *spi_parts;
	struct device_node *np = spi->dev.of_node;
	char *propname;
	int ret;
	phandle phandle;
	struct device_node *np_spi_part;
#endif
	int flag = -1;
	flag = check_storage_device();
	printk("%s\n", __func__);
	if(flag < 0){
		printk("%s %d boot_device_flag %d : do not init spi\n",__func__,__LINE__,boot_device_flag);
		return  -ENOMEM;
	}
	
#ifdef CONFIG_OF

	if(spi->dev.of_node){
		data = kzalloc(sizeof(struct flash_platform_data), GFP_KERNEL);
		if(!data){
				printk("%s amlogic_spi_platform alloc err\n",__func__);
				return -1;
		}
		
		ret = of_property_read_u32(np,"nr-parts",&data->nr_parts);
		if(ret){
			printk("%s:%d,please config nr-parts item\n",__func__,__LINE__);
			return -1;
		}

		if(data->nr_parts > 0)
		{
			spi_parts = kzalloc(sizeof(struct mtd_partition)*data->nr_parts, GFP_KERNEL);
			if(!spi_parts){
				printk("%s spi_parts alloc err\n",__func__);
				kfree(data);
				return -1;
			}
			data->parts = spi_parts;
		}
		
		for(index = 0; index < data->nr_parts; index++)
		{
			propname = kasprintf(GFP_KERNEL, "nr-part-%d", index);
			ret = of_property_read_u32(np,propname,&val);
			if(ret){
				printk("don't find  match nr-part-%d\n",index);
				goto err;
			}
			if(ret==0){
				phandle = val;
				np_spi_part = of_find_node_by_phandle(phandle);
				if(!np_spi_part){
					printk("%s:%d,can't find device node\n",__func__,__LINE__);
				goto err;
				}
			}
			ret = of_property_read_string(np_spi_part, "name", (const char **)&(data->parts[index].name));
			if(ret){
				printk("%s:%d,please config name item\n",__func__,__LINE__);
				goto err;
			}
			ret = of_property_read_u32(np_spi_part, "offset", (u32 *)&(data->parts[index].offset));
			if(ret){
				printk("%s:%d,please config offset item\n",__func__,__LINE__);
				goto err;
			}
			ret = of_property_read_u32(np_spi_part, "size", (u32 *)&(data->parts[index].size));
			if(ret){
				printk("%s:%d,please config size item\n",__func__,__LINE__);
				goto err;
			}
		}
		spi->dev.platform_data = data;		
	}
#else
	data = spi->dev.platform_data;
#endif
	if (data && data->type) {		
		for (i = 0, info = spi_nor_data;
				i < ARRAY_SIZE(spi_nor_data);
				i++, info++) {
			if (strcmp(data->type, info->name) == 0)
				break;
		}

		/* unrecognized chip? */
		if (i == ARRAY_SIZE(spi_nor_data)) {			
			info = jedec_probe(spi);
			if (!info) {
				pr_debug("%s: unrecognized id %s\n",
						dev_name(&spi_nor->spi->dev), data->type);
				info = NULL;
			}

		/* recognized; is that chip really what's there? */
		} else if ((i == ARRAY_SIZE(spi_nor_data)) && (info->jedec_id)) {			
			struct flash_info	*chip = jedec_probe(spi);

			if (!chip || chip != info) {
				dev_warn(&spi->dev, "found %s, expected %s\n",
						chip ? chip->name : "UNKNOWN",
						info->name);
				info = NULL;
			}
		}
	} else		
		info = jedec_probe(spi);	

	if (!info)
		return -ENODEV;

	spi_nor = kzalloc(sizeof *spi_nor, GFP_KERNEL);
	if (!spi_nor)
		return -ENOMEM;

	spi_nor->spi = spi;
	mutex_init(&spi_nor->lock);
	dev_set_drvdata(&spi->dev, spi_nor);

	if (data && data->name)
		spi_nor->mtd.name = data->name;
	else
		spi_nor->mtd.name = dev_name(&spi_nor->spi->dev);

	//for sst flash pp command just one byte a time
	if (((info->jedec_id & 0xff0000)>>16) == 0xbf)
		spi_nor->flash_pp_size = 1;
	else
		spi_nor->flash_pp_size = FLASH_PAGESIZE;

	spi_nor->mtd.type = MTD_NORFLASH;
	spi_nor->mtd.flags = MTD_CAP_NORFLASH;
	spi_nor->mtd.size = info->block_size * info->n_blocks;
	spi_nor->mtd._erase = spi_nor_erase;
	spi_nor->mtd._read = spi_nor_read;
	spi_nor->mtd._write = spi_nor_write;

	/* prefer "small sector" erase if possible */
	if (data) {		
		spi_nor->mtd.writesize = info->block_size;
		
		spi_nor->mtd.erasesize = 4096;
		if(!(info->flags & SECT_4K) && !(info->flags & BLOCK_64K))
			spi_nor->mtd.erasesize = info->block_size;		
	
		if (spi_nor->mtd.erasesize == 0x1000)
			spi_nor->erase_opcode = OPCODE_SE_4K;
		else if (spi_nor->mtd.erasesize == 0x10000)
			spi_nor->erase_opcode = OPCODE_BE;
		else
			spi_nor->erase_opcode = OPCODE_BE;
	}
	else {
		if (info->flags & SECT_4K) {			
			spi_nor->mtd.writesize = 1;
			spi_nor->erase_opcode = OPCODE_SE_4K;
			spi_nor->mtd.erasesize = 4096;
		} else if (info->flags & BLOCK_64K) {			
			spi_nor->mtd.writesize = 4096;
			spi_nor->erase_opcode = OPCODE_BE;
			spi_nor->mtd.erasesize = info->block_size;
		}
		else {			
			spi_nor->mtd.writesize = 1;
			spi_nor->erase_opcode = OPCODE_BE;
			spi_nor->mtd.erasesize = info->block_size;
		}
	}

	dev_info(&spi->dev, "%s (%lld Kbytes)\n", info->name,
			(long long)spi_nor->mtd.size / 1024);

	pr_debug(
		"mtd .name = %s, .size = 0x%llx (%lldMiB) "
			".erasesize = 0x%.8x (%uKiB) .numeraseregions = %d\n",
		spi_nor->mtd.name,
		(long long)spi_nor->mtd.size, (long long)spi_nor->mtd.size / (1024*1024),
		spi_nor->mtd.erasesize, spi_nor->mtd.erasesize / 1024,
		spi_nor->mtd.numeraseregions);

	if (spi_nor->mtd.numeraseregions)
		for (i = 0; i < spi_nor->mtd.numeraseregions; i++)
			pr_debug(
				"mtd.eraseregions[%d] = { .offset = 0x%llx, "
				".erasesize = 0x%.8x (%uKiB), "
				".numblocks = %d }\n",
				i, spi_nor->mtd.eraseregions[i].offset,
				spi_nor->mtd.eraseregions[i].erasesize,
				spi_nor->mtd.eraseregions[i].erasesize / 1024,
				spi_nor->mtd.eraseregions[i].numblocks);


	/* partitions should match sector boundaries; and it may be good to
	 * use readonly partitions for writeprotected sectors (BP2..BP0).
	 */
	if (mtd_has_partitions()) {
#ifdef CONFIG_MTD
	parts = data->parts;
	nr = data->nr_parts;

//	return add_mtd_partitions(&spi_nor->mtd, parts, nr);  //3.0.50
	return mtd_device_register(&spi_nor->mtd, parts, nr);
#else
//	return add_mtd_device(&spi_nor->mtd) == 1 ? -ENODEV : 0;  //3.0.50
	return mtd_device_register(&spi_nor->mtd,NULL,0);
#endif
	}else{
		return 0;
	}

err:
	kfree(spi_parts);
	kfree(data);
	return -1;
}


static int spi_nor_remove(struct spi_device *spi)
{
	struct spi_nor	*spi_nor;
	int		status;
	spi_nor = dev_get_drvdata(&spi->dev);

	/* Clean up MTD stuff. */
	if (mtd_has_partitions() && spi_nor->partitioned)
//		status = del_mtd_partitions(&spi_nor->mtd); //3.0.50
		status = mtd_device_unregister(&spi_nor->mtd);
	else
//		status = del_mtd_device(&spi_nor->mtd); //3.0.50
		status = mtd_device_unregister(&spi_nor->mtd);
	if (status == 0) {
	#ifdef CONFIG_OF
		kfree(spi->dev.platform_data);
	#endif
		kfree(spi_nor);
	}
	return 0;
}


static struct spi_driver spi_nor_driver = {
	.driver = {
		.name	= "spi_nor",
		.bus	= &spi_bus_type,
		.owner	= THIS_MODULE,
	},
	.probe	= spi_nor_probe,
	.remove	= spi_nor_remove,

	/* REVISIT: many of these chips have deep power-down modes, which
	 * should clearly be entered on suspend() to minimize power use.
	 * And also when they're otherwise idle...
	 */
};


static int __init spi_nor_init(void)
{
	printk("%s\n", __func__);
	return spi_register_driver(&spi_nor_driver);
}


static void __exit spi_nor_exit(void)
{
	spi_unregister_driver(&spi_nor_driver);
}


module_init(spi_nor_init);
module_exit(spi_nor_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MTD SPI driver for SPI NOR flash chips");
