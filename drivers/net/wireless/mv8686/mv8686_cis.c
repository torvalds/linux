#include <linux/kernel.h>

#include <linux/mmc/host.h>
#include <linux/mmc/card.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/sdio_func.h>

/*
 * common CIS.
 */
unsigned char mv8686_cis_vers1[32] =  //0x15
{0x01, 0x00, 0x4d, 0x61, 0x72, 0x76, 0x65, 0x6c, 0x6c, 0x00, 0x38, 0x30, 0x32, 
 0x2e, 0x31, 0x31, 0x20, 0x53, 0x44, 0x49, 0x4f, 0x20, 0x49, 0x44, 0x3a, 0x20, 
 0x30, 0x42, 0x00, 0x00, 0xff
};
unsigned char mv8686_cis_manfid[4] = {0xdf, 0x02, 0x03, 0x91}; //0x20
unsigned char mv8686_cis_funcid[4] = {0x0c, 0x00}; //0x21
unsigned char mv8686_cis_funce[4] = {0x00, 0x00, 0x01, 0x32}; //0x22

/*
 * Function CIS.
 */
unsigned char mv8686_fcis_funcid[4] = {0x0c, 0x00}; //0x21
unsigned char mv8686_fcis_funce[28] = //0x22
{0x01, 0x01, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 
 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}; 

static int mv8686_cistpl_vers_1(struct mmc_card *card, struct sdio_func *func,
			 const unsigned char *buf, unsigned size)
{
	unsigned i, nr_strings;
	char **buffer, *string;

	buf += 2;
	size -= 2;
  
/*
	printk("CIS VERS 1: ");
	for (i = 0; i < size; i++)
		printk("%2.2x ", buf[i]);
	printk("\n");
*/

	nr_strings = 0;
	for (i = 0; i < size; i++)
	{
		if (buf[i] == 0xff)
			break;
			
		if (buf[i] == 0)
			nr_strings++;
	}

	if (buf[i-1] != '\0') 
	{
		printk(KERN_WARNING "SDIO: ignoring broken CISTPL_VERS_1\n");
		return 0;
	}

	size = i;

	buffer = kzalloc(sizeof(char*) * nr_strings + size, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	string = (char*)(buffer + nr_strings);

	for (i = 0; i < nr_strings; i++) 
	{
		buffer[i] = string;
		strcpy(string, buf);
		string += strlen(string) + 1;
		buf += strlen(buf) + 1;
	}

	if (func) 
	{
		func->num_info = nr_strings;
		func->info = (const char**)buffer;
	} else {
		card->num_info = nr_strings;
		card->info = (const char**)buffer;
	}

	return 0;
}

static int mv8686_cistpl_manfid(struct mmc_card *card, struct sdio_func *func,
			 const unsigned char *buf, unsigned size)
{
	unsigned int vendor, device;

	/* TPLMID_MANF */
	vendor = buf[0] | (buf[1] << 8);

	/* TPLMID_CARD */
	device = buf[2] | (buf[3] << 8);

	if (func) {
		func->vendor = vendor;
		func->device = device;
	} else {
		card->cis.vendor = vendor;
		card->cis.device = device;
	}

	return 0;
}

static const unsigned char mv8686_speed_val[16] =
	{ 0, 10, 12, 13, 15, 20, 25, 30, 35, 40, 45, 50, 55, 60, 70, 80 };
static const unsigned int mv8686_speed_unit[8] =
	{ 10000, 100000, 1000000, 10000000, 0, 0, 0, 0 };

static int mv8686_cistpl_funce_common(struct mmc_card *card,
			       const unsigned char *buf, unsigned size)
{
	if (size < 0x04 || buf[0] != 0)
		return -EINVAL;

	/* TPLFE_FN0_BLK_SIZE */
	card->cis.blksize = buf[1] | (buf[2] << 8);

	/* TPLFE_MAX_TRAN_SPEED */
	card->cis.max_dtr = mv8686_speed_val[(buf[3] >> 3) & 15] *
			    mv8686_speed_unit[buf[3] & 7];

	return 0;
}

static int mv8686_cistpl_funce_func(struct sdio_func *func,
			     const unsigned char *buf, unsigned size)
{
	unsigned vsn;
	unsigned min_size;

	vsn = func->card->cccr.sdio_vsn;
	min_size = (vsn == SDIO_SDIO_REV_1_00) ? 28 : 42;

	if (size < min_size || buf[0] != 1)
		return -EINVAL;

	/* TPLFE_MAX_BLK_SIZE */
	func->max_blksize = buf[12] | (buf[13] << 8);

	return 0;
}

static int mv8686_cistpl_funce(struct mmc_card *card, struct sdio_func *func,
			const unsigned char *buf, unsigned size)
{
	int ret;

	/*
	 * There should be two versions of the CISTPL_FUNCE tuple,
	 * one for the common CIS (function 0) and a version used by
	 * the individual function's CIS (1-7). Yet, the later has a
	 * different length depending on the SDIO spec version.
	 */
	if (func)
		ret = mv8686_cistpl_funce_func(func, buf, size);
	else
		ret = mv8686_cistpl_funce_common(card, buf, size);

	if (ret) {
		printk(KERN_ERR "%s: bad CISTPL_FUNCE size %u "
		       "type %u\n", mmc_hostname(card->host), size, buf[0]);
		return ret;
	}

	return 0;
}

int mv8686_read_common_cis(struct mmc_card *card)
{
	mv8686_cistpl_vers_1(card, NULL, mv8686_cis_vers1, 31);
	mv8686_cistpl_manfid(card, NULL, mv8686_cis_manfid, 4);
	mv8686_cistpl_funce(card, NULL, mv8686_cis_funce, 4);
	
	return 0;
}

int mv8686_read_func_cis(struct sdio_func *func)
{
	mv8686_cistpl_funce(func->card, func, mv8686_fcis_funce, 28);
	
	return 0;
}
