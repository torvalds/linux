#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h> // udelay()
#include <linux/device.h> // device_create()
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/version.h>      /* constant of kernel version */
#include <asm/uaccess.h> // get_user()

#include <linux/fm.h>
#include <mach/mt6575_gpio.h>
#include <mach/mtk_rtc.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/delay.h>

// if need debug, define FMDEBUG
//#define FMDEBUG

// if your platform is MT6515/6575, define MTK_MT6515 
#define MTK_MT6515

// if your platform is MT6515/6575 and MTK FM is MT6626, define MT6626 
//#define MT6626

#define FM_ALERT(f, s...) \
	do { \
		printk(KERN_ALERT "RDAFM " f, ## s); \
	} while(0)

#ifdef FMDEBUG
#define FM_DEBUG(f, s...) \
	do { \
		printk("RDAFM " f, ## s); \
	} while(0)
#else
#define FM_DEBUG(f, s...)
#endif

#define RDA599X_SCANTBL_SIZE  16 //16*uinit16_t
#define RDA599X_FM_SCAN_UP       0x0
#define RDA599X_FM_SCAN_DOWN     0x01

extern int rda_gpio_i2c_read_1_addr_2_data(uint8_t chipAddr, uint8_t regAddr, unsigned short *buffer);
extern int rda_gpio_i2c_write_1_addr_2_data(uint8_t chipAddr, uint8_t regAddr, unsigned short data);
extern int rda_fm_power_off(void);
extern int rda_fm_power_on(void);

/******************************************************************************
 * CONSTANT DEFINITIONS
 *****************************************************************************/
#define RDAFM_SLAVE_ADDR        0x11    //RDA FM Chip address

#define RDAFM_MASK_RSSI         0X7F // RSSI



#define ID_RDA5802E             0x5804
#define ID_RDA5802H             0x5801
#define ID_RDA5802N             0x5808
#define ID_RDA5820              0x5805
#define ID_RDA5820NS            0x5820



static struct proc_dir_entry *g_fm_proc = NULL;
static struct fm *g_fm_struct = NULL;
static atomic_t scan_complete_flag;

#define FM_PROC_FILE "fm"

/******************************************************************************
 * STRUCTURE DEFINITIONS
 *****************************************************************************/

enum RDAFM_CHIP_TYPE {
	CHIP_TYPE_RDA5802E = 0,
	CHIP_TYPE_RDA5802H,
	CHIP_TYPE_RDA5802N,
	CHIP_TYPE_RDA5820,
	CHIP_TYPE_RDA5820NS,
};


typedef struct
{
	uint8_t		address;
	uint16_t	value;
}RDA_FM_REG_T;   

typedef struct
{
	bool		byPowerUp;
	struct fm_tune_parm parm
}FM_TUNE_T;
static FM_TUNE_T fm_tune_data = {false, {}};

typedef enum
{
	FM_RECEIVER,				//5800,5802,5804
	FM_TRANSMITTER,            	//5820
}RDA_RADIO_WORK_E;

typedef enum
{
	OFF,
	ON,
}RDA_FM_POWER_STATE_T;

struct fm {
	uint32_t ref;
	bool powerup;
	uint16_t chip_id;
	//    uint16_t device_id;
	uint8_t chipAddr;
	dev_t dev_t;
	uint16_t min_freq; // KHz
	uint16_t max_freq; // KHz
	uint8_t band;   // TODO
	struct class *cls;
	struct device *dev;
	struct cdev cdev;
	//    struct i2c_client *i2c_client;
};




/******************************************************************************
 * FUNCTION PROTOTYPES
 *****************************************************************************/


static int RDAFM_clear_hmute(uint8_t chipAddr);
static int RDAFM_enable_hmute(uint8_t chipAddr);
static bool RDAFM_Scan(uint8_t chipAddr,
		uint16_t min_freq, uint16_t max_freq,
		uint16_t *pFreq, //get the valid freq after scan
		uint16_t *pScanTBL,
		uint16_t *ScanTBLsize,
		uint16_t scandir,
		uint16_t space);


static int RDAFM_read(uint8_t chipAddr, uint8_t addr, uint16_t *val);
static int RDAFM_write(uint8_t chipAddr, uint8_t addr, uint16_t val);
static int fm_setup_cdev(struct fm *fm);
static int fm_ops_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
static loff_t fm_ops_lseek(struct file *filp, loff_t off, int whence);
static int fm_ops_open(struct inode *inode, struct file *filp);
static int fm_ops_release(struct inode *inode, struct file *filp);

static int fm_init(void);
static int fm_destroy(struct fm *fm);
static int fm_powerup(struct fm *fm, struct fm_tune_parm *parm);
static int fm_powerdown(struct fm *fm);

static int fm_tune(struct fm *fm, struct fm_tune_parm *parm);
static int fm_seek(struct fm *fm, struct fm_seek_parm *parm);
static int fm_scan(struct fm *fm, struct fm_scan_parm *parm);
static int fm_setvol(struct fm *fm, uint32_t vol);
static int fm_getvol(struct fm *fm, uint32_t *vol);
static int fm_getrssi(struct fm *fm, uint32_t *rssi);
static int fm_proc_read(char *page, char **start, off_t off, int count, int *eof, void *data);






static uint16_t RDAFM_CHIP_ID = 0x5808;
static RDA_RADIO_WORK_E RDA_RADIO_WorkType = FM_RECEIVER;



#if 1
static uint16_t RDA5802N_initialization_reg[]={
	0xC005, //02h 
	0x0000,
	0x0400,
	0xC6ED, //0x86AD, //05h
	0x6000,
	0x721A, //0x42C6
	0x0000,
	0x0000,
	0x0000,  //0x0ah
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,  //0x10h
	0x0019,
	0x2A11,
	0xB042,  
	0x2A11,  
	0xB831,  //0x15h 
	0xC000,
	0x2A91,
	0x9400,
	0x00A8,
	0xc400,  //0x1ah
	0xF7CF,  //提高远端噪声抑制  
	0x2414, //0x2ADC,  //0x1ch 提升VIO VDD之间压差引起的不良
	0x806F, 
	0x4608,
	0x0086,
	0x0661, //0x20H
	0x0000,
	0x109E,
	0x23C8,
	0x0406,
	0x0E1C, //0x25H
};
#else
static uint16_t RDA5802N_initialization_reg[]={
	0xc401, //02h
	0x0000,
	0x0400,
	0x86ad, //05h//
	0x0000,
	0x42c6,
	0x0000,
	0x0000,
	0x0000,  //0x0ah
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,  //0x10h
	0x0019,  
	0x2a11,
	0xa053,//0x80,0x53,
	0x3e11,//0x22,0x11,	
	0xfc7d,  //0x15h 
	0xc000,
	0x2a91,
	0x9400,
	0x00a8,
	0xc400,  //0x1ah
	0xe000,
	0x2b1d, //0x23,0x14
	0x816a,
	0x4608,
	0x0086,
	0x0661,  //0x20h
	0x0000,  
	0x109e,
	0x2244,
	0x0408,  //0x24
	0x0408,  //0x25
};
#endif

static RDA_FM_REG_T RDA5820NS_TX_initialization_reg[]={
	{0x02, 0xE003},
	{0xFF, 100},    // if address is 0xFF, sleep value ms
	{0x02, 0xE001},
	{0x19, 0x88A8},
	{0x1A, 0x4290},
	{0x68, 0x0AF0},
	{0x40, 0x0001},
	{0x41, 0x41FF},
	{0xFF, 500},
	{0x03, 0x1B90},
};

static RDA_FM_REG_T RDA5820NS_RX_initialization_reg[]={
	{0x02, 0x0002}, //Soft reset
	{0xFF, 100},    // wait
	{0x02, 0xC001},  //Power Up 
	{0x05, 0x888F},  //LNAP  0x884F --LNAN
	{0x06, 0x6000},
	{0x13, 0x80E1},
	{0x14, 0x2A11},
	{0x1C, 0x22DE},
	{0x21, 0x0020},
	{0x03, 0x1B90},
};



static struct file_operations fm_ops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = fm_ops_ioctl,
	.llseek = fm_ops_lseek,
	.open = fm_ops_open,
	.release = fm_ops_release,
};

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,36)
static DECLARE_MUTEX(fm_ops_mutex);
#else
DEFINE_SEMAPHORE(fm_ops_mutex);
#endif

static int RDAFM_GetChipID(uint8_t chipAddr, uint16_t *pChipID)
{
	int err;
	int ret = -1;
	uint16_t val = 0x0002;

	//Reset RDA FM
	err = RDAFM_write(chipAddr, 0x02, val);
	if(err < 0){
#ifdef FMDEBUG
		FM_DEBUG("RDAFM_GetChipID: reset FM chip failed!\n");
#endif
		ret = -1;
		return ret;
	}
	msleep(80);

	val = 0;
	err = RDAFM_read(chipAddr, 0x0C, &val);
	if (err == 0)
	{
		if ((0x5802 == val) || (0x5803 == val))
		{
			err = RDAFM_read(chipAddr, 0x0E, &val);

			if (err == 0)
				*pChipID = val;
			else
				*pChipID = 0x5802;

#ifdef FMDEBUG
			FM_DEBUG("RDAFM_GetChipID: Chip ID = %04X\n", val);
#endif

			ret = 0;

		}
		else if ((0x5805 == val) || (0x5820 == val))
		{
			*pChipID = val;
			ret = 0;
		}
		else
		{
#ifdef FMDEBUG
			FM_DEBUG("RDAFM_GetChipID: get chip ID failed! get value = %04X\n", val);
#endif
			ret = -1;
		}

	}
	else
	{
#ifdef FMDEBUG
		FM_DEBUG("RDAFM_GetChipID: get chip ID failed!\n");
#endif
		ret = -1;
	}

	return ret;
}


/*
 *  RDAFM_read
 */
static int RDAFM_read(uint8_t chipAddr, uint8_t regAddr, uint16_t *val)
{
	int ret = -1;
	ret = rda_gpio_i2c_read_1_addr_2_data(chipAddr, regAddr, val);

	return ret;
}

/*
 *  RDAFM_write
 */
static int RDAFM_write(uint8_t chipAddr, uint8_t regAddr, uint16_t val)
{
	int n;

	n = rda_gpio_i2c_write_1_addr_2_data(chipAddr, regAddr, val);
	if (n < 0)
	{
		FM_ALERT("RDAFM_write send:0x%X err:%d\n", regAddr, n);
		return -1;
	}

	return 0;
}


static int RDAFM_clear_hmute(uint8_t chipAddr)
{
	int ret = 0;
	uint16_t tRegValue = 0;

	FM_DEBUG("RDAFM_clear_hmute\n");

	ret = RDAFM_read(chipAddr, 0x02, &tRegValue);
	if (ret < 0)
	{
		FM_ALERT("RDAFM_clear_hmute  read register failed!\n"); 
		return -1;
	}

	tRegValue |= (1 << 14);

	ret = RDAFM_write(chipAddr, 0x02, tRegValue);

	if (ret < 0)
	{
		FM_ALERT("RDAFM_clear_hmute  write register failed!\n"); 
		return -1;
	}

	if(fm_tune_data.byPowerUp){
		if (fm_tune(g_fm_struct, &(fm_tune_data.parm)) < 0)
		{
			fm_tune_data.byPowerUp = false;
			memset(&fm_tune_data.parm, 0, sizeof(fm_tune_data.parm));
			return -EPERM;
		}
		fm_tune_data.byPowerUp = false;
		memset(&fm_tune_data.parm, 0, sizeof(fm_tune_data.parm));
	}

	return 0;
}



static int RDAFM_enable_hmute(uint8_t chipAddr)
{
	int ret = 0;
	uint16_t tRegValue = 0;

	FM_DEBUG("RDAFM_enable_hmute\n");

	ret = RDAFM_read(chipAddr, 0x02, &tRegValue);
	if (ret < 0)
	{
		FM_ALERT("RDAFM_enable_hmute  read register failed!\n"); 
		return -1;
	}

	tRegValue &= (~(1 << 14));

	ret = RDAFM_write(chipAddr, 0x02, tRegValue);

	if (ret < 0)
	{
		FM_ALERT("RDAFM_enable_hmute  write register failed!\n"); 
		return -1;
	}

	return 0;
}


static bool RDAFM_Scan(uint8_t chipAddr, 
		uint16_t min_freq, uint16_t max_freq,
		uint16_t *pFreq,
		uint16_t *pScanTBL, 
		uint16_t *ScanTBLsize, 
		uint16_t scandir, 
		uint16_t space)
{
	uint16_t tFreq, tRegValue = 0;
	uint16_t tmp_scanTBLsize = *ScanTBLsize;
	int ret = -1;
	bool isTrueStation = false;
	uint16_t oldValue = 0;
	int channel = 0;

	if((!pScanTBL) || (tmp_scanTBLsize == 0)) {
		return false;
	}

	//clear the old value of pScanTBL
	memset(pScanTBL, 0, sizeof(uint16_t)*RDA599X_SCANTBL_SIZE);

	if(tmp_scanTBLsize > RDA599X_SCANTBL_SIZE)
	{
		tmp_scanTBLsize = RDA599X_SCANTBL_SIZE;
	}

	//scan up
	if(scandir == RDA599X_FM_SCAN_UP){ // now, only support scan up
		tFreq = min_freq;
	}else{ //scan down
		tFreq = max_freq;//max_freq compare need or not   
	}

	//mute FM
	RDAFM_enable_hmute(chipAddr);

	//set seekth
	tRegValue = 0;
	RDAFM_read(chipAddr, 0x05, &tRegValue);
	tRegValue &= (~(0x7f<<8));
	tRegValue |= ((0x8 & 0x7f) << 8);
	RDAFM_write(chipAddr, 0x05, tRegValue);
	msleep(50);

	atomic_set(&scan_complete_flag, 1);

	do {
		if(atomic_read(&scan_complete_flag) == 0)
			break;
		isTrueStation = false;

		//set channel and enable TUNE
		tRegValue = 0;
		RDAFM_read(chipAddr, 0x03, &tRegValue);
		tRegValue &= (~(0x03ff<<6)); //clear bit[15:6]
		channel = tFreq - min_freq; 
		tRegValue |= ((channel << 6) | (1 << 4)); //set bit[15:6] and bit[4]
		ret = RDAFM_write(chipAddr, 0x03, tRegValue);
		msleep(40);

		//read 0x0B and check FM_TRUE(bit[8])
		tRegValue = 0;
		ret = RDAFM_read(chipAddr, 0x0B, &tRegValue);
		if(!ret){
			if((tRegValue & 0x0100) == 0x0100){
				isTrueStation = true;
			}
		}

		//if this freq is a true station, read the channel
		if(isTrueStation){
			//tRegValue = 0;
			//RDAFM_read(chipAddr, 0x0A, &tRegValue);
			//channel = ((tRegValue) & 0x03ff) - 5;
			channel = channel - 5;
			if((channel >= 0) && (channel != 85)){
				oldValue = *(pScanTBL+(channel/16));
				oldValue |= (1<<(channel%16));
				*(pScanTBL+(channel/16)) = oldValue;
			}
		}

		//increase freq
		tFreq += space;
	}while( tFreq <= max_freq );

#if defined(MTK_MT6515) && defined(MT6626)
	*(pScanTBL+13) = 0xb2d4;
	*(pScanTBL+14) = 0xb2d4;
	*(pScanTBL+15) = 0xb2d4;
#endif

	*ScanTBLsize = tmp_scanTBLsize;
	*pFreq = 0;

	//clear FM mute
	RDAFM_clear_hmute(chipAddr);

	return true;
}


static int fm_setup_cdev(struct fm *fm)
{
	int err;

	err = alloc_chrdev_region(&fm->dev_t, 0, 1, FM_NAME);
	if (err) {
		FM_ALERT("alloc dev_t failed\n");
		return -1;
	}

	FM_ALERT("alloc %s:%d:%d\n", FM_NAME,
			MAJOR(fm->dev_t), MINOR(fm->dev_t));

	cdev_init(&fm->cdev, &fm_ops);

	fm->cdev.owner = THIS_MODULE;
	fm->cdev.ops = &fm_ops;

	err = cdev_add(&fm->cdev, fm->dev_t, 1);
	if (err) {
		FM_ALERT("alloc dev_t failed\n");
		return -1;
	}

	fm->cls = class_create(THIS_MODULE, FM_NAME);
	if (IS_ERR(fm->cls)) {
		err = PTR_ERR(fm->cls);
		FM_ALERT("class_create err:%d\n", err);
		return err;            
	}    
	fm->dev = device_create(fm->cls, NULL, fm->dev_t, NULL, FM_NAME);

	return 0;
}



static int fm_ops_ioctl(struct file *filp,
		unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	struct fm *fm = container_of(filp->f_dentry->d_inode->i_cdev, struct fm, cdev);

	FM_DEBUG("%s cmd(%x)\n", __func__, cmd);

	switch(cmd)
	{
		case FM_IOCTL_POWERUP:
			{
				struct fm_tune_parm parm;
				FM_DEBUG("FM_IOCTL_POWERUP\n");

				if (copy_from_user(&parm, (void*)arg, sizeof(struct fm_tune_parm)))
					return -EFAULT;

				if (down_interruptible(&fm_ops_mutex))
					return -EFAULT;
				ret = fm_powerup(fm, &parm);
				up(&fm_ops_mutex);
				if (copy_to_user((void*)arg, &parm, sizeof(struct fm_tune_parm)))
					return -EFAULT;
				break;
			}

		case FM_IOCTL_POWERDOWN:
			{
				FM_DEBUG("FM_IOCTL_POWERDOWN\n");
				if (down_interruptible(&fm_ops_mutex))
					return -EFAULT;
				ret = fm_powerdown(fm);
				up(&fm_ops_mutex);
				break;
			}

			// tune (frequency, auto Hi/Lo ON/OFF )
		case FM_IOCTL_TUNE:
			{
				struct fm_tune_parm parm;
				FM_DEBUG("FM_IOCTL_TUNE\n");

				if (copy_from_user(&parm, (void*)arg, sizeof(struct fm_tune_parm)))
					return -EFAULT;

				if (down_interruptible(&fm_ops_mutex))
					return -EFAULT;
				ret = fm_tune(fm, &parm);
				up(&fm_ops_mutex);

				if (copy_to_user((void*)arg, &parm, sizeof(struct fm_tune_parm)))
					return -EFAULT;

				break;
			}

		case FM_IOCTL_SEEK:
			{
				struct fm_seek_parm parm;
				FM_DEBUG("FM_IOCTL_SEEK\n");

				if (copy_from_user(&parm, (void*)arg, sizeof(struct fm_seek_parm)))
					return -EFAULT;

				if (down_interruptible(&fm_ops_mutex))
					return -EFAULT;
				ret = fm_seek(fm, &parm);
				up(&fm_ops_mutex);

				if (copy_to_user((void*)arg, &parm, sizeof(struct fm_seek_parm)))
					return -EFAULT;

				break;
			}

		case FM_IOCTL_SETVOL:
			{
				uint32_t vol;
				FM_DEBUG("FM_IOCTL_SETVOL\n");

				if(copy_from_user(&vol, (void*)arg, sizeof(uint32_t))) {
					FM_ALERT("copy_from_user failed\n");
					return -EFAULT;
				}

				if (down_interruptible(&fm_ops_mutex))
					return -EFAULT;
				ret = fm_setvol(fm, vol);
				up(&fm_ops_mutex);

				break;
			}

		case FM_IOCTL_GETVOL:
			{
				uint32_t vol;
				FM_DEBUG("FM_IOCTL_GETVOL\n");

				if (down_interruptible(&fm_ops_mutex))
					return -EFAULT;
				ret = fm_getvol(fm, &vol);
				up(&fm_ops_mutex);

				if (copy_to_user((void*)arg, &vol, sizeof(uint32_t)))
					return -EFAULT;

				break;
			}

		case FM_IOCTL_MUTE:
			{
				uint32_t bmute;
				FM_DEBUG("FM_IOCTL_MUTE\n");

				if (copy_from_user(&bmute, (void*)arg, sizeof(uint32_t)))
				{
					FM_DEBUG("copy_from_user mute failed!\n");
					return -EFAULT;    
				}

				FM_DEBUG("FM_IOCTL_MUTE:%d\n", bmute); 
				if (down_interruptible(&fm_ops_mutex))
					return -EFAULT;

				if (bmute){
					ret = RDAFM_enable_hmute(fm->chipAddr);
				}else{
					ret = RDAFM_clear_hmute(fm->chipAddr);
				}

				up(&fm_ops_mutex);

				break;
			}

		case FM_IOCTL_GETRSSI:
			{
				uint32_t rssi;
				FM_DEBUG("FM_IOCTL_GETRSSI\n");

				if (down_interruptible(&fm_ops_mutex))
					return -EFAULT;

				ret = fm_getrssi(fm, &rssi);
				up(&fm_ops_mutex);

				if (copy_to_user((void*)arg, &rssi, sizeof(uint32_t)))
					return -EFAULT;

				break;
			}

		case FM_IOCTL_RW_REG:
			{
				struct fm_ctl_parm parm_ctl;
				FM_DEBUG("FM_IOCTL_RW_REG\n");

				if (copy_from_user(&parm_ctl, (void*)arg, sizeof(struct fm_ctl_parm)))
					return -EFAULT;

				if (down_interruptible(&fm_ops_mutex))
					return -EFAULT;

				if(parm_ctl.rw_flag == 0) //write
				{
					ret = RDAFM_write(fm->chipAddr, parm_ctl.addr, parm_ctl.val);
				}
				else
				{
					ret = RDAFM_read(fm->chipAddr, parm_ctl.addr, &parm_ctl.val);
				}

				up(&fm_ops_mutex);
				if ((parm_ctl.rw_flag == 0x01) && (!ret)) // Read success.
				{ 
					if (copy_to_user((void*)arg, &parm_ctl, sizeof(struct fm_ctl_parm)))
						return -EFAULT;
				}
				break;
			}

		case FM_IOCTL_GETCHIPID:
			{
				uint16_t chipid;            

				if (down_interruptible(&fm_ops_mutex))
					return -EFAULT;

				RDAFM_GetChipID(fm->chipAddr, &chipid);
				//chipid = fm->chip_id;
				chipid = 0x6620;
				FM_DEBUG("FM_IOCTL_GETCHIPID:%04x\n", chipid);   
				up(&fm_ops_mutex);

				if (copy_to_user((void*)arg, &chipid, sizeof(uint16_t)))
					return -EFAULT;

				break;
			}

		case FM_IOCTL_IS_FM_POWERED_UP:
			{
				uint32_t powerup;
				FM_DEBUG("FM_IOCTL_IS_FM_POWERED_UP");
				if (fm->powerup) {
					powerup = 1;
				} else {
					powerup = 0;
				}
				if (copy_to_user((void*)arg, &powerup, sizeof(uint32_t)))
					return -EFAULT;
				break;
			}

#ifdef FMDEBUG
		case FM_IOCTL_DUMP_REG:
			{
				uint16_t chipid = 0;
				if (down_interruptible(&fm_ops_mutex))
					return -EFAULT;
				RDAFM_GetChipID(fm->chipAddr, &chipid);
				up(&fm_ops_mutex);

				break;
			}
#endif

		case FM_IOCTL_SCAN:
			{
				struct fm_scan_parm parm;
				FM_DEBUG("FM_IOCTL_SCAN\n");
				if (false == fm->powerup){
					return -EFAULT;
				}
				if(copy_from_user(&parm, (void*)arg, sizeof(struct fm_scan_parm))){
					return -EFAULT;
				}
				if (down_interruptible(&fm_ops_mutex)){
					return -EFAULT;
				}
				fm_scan(fm, &parm);
				up(&fm_ops_mutex);

				if(copy_to_user((void*)arg, &parm, sizeof(struct fm_scan_parm))){
					return -EFAULT;
				}

				break;
			}

		case FM_IOCTL_STOP_SCAN:
			{
				FM_DEBUG("FM_IOCTL_STOP_SCAN\n");
				break;
			}

		case FM_IOCTL_SCAN_GETRSSI:
			{
				FM_DEBUG("FM_IOCTL_SCAN_GETRSSI\n");
				break;
			}

		case FM_IOCTL_GETMONOSTERO:
			{
				FM_DEBUG("FM_IOCTL_GETMONOSTERO\n");
				break;
			}

		case FM_IOCTL_SETMONOSTERO:
			{
				FM_DEBUG("FM_IOCTL_SETMONOSTERO\n");
				break;
			}

		case FM_IOCTL_GETCURPAMD:
			{
				FM_DEBUG("FM_IOCTL_GETCURPAMD\n");
				break;
			}

		case FM_IOCTL_EM_TEST:
			{
				FM_DEBUG("FM_IOCTL_EM_TEST\n");
				break;
			}

		case FM_IOCTL_RDS_SUPPORT:
			{
				FM_DEBUG("FM_IOCTL_RDS_SUPPORT\n");
				break;
			}

		case FM_IOCTL_RDS_ONOFF:
			{
				FM_DEBUG("FM_IOCTL_RDS_ONOFF\n");
				break;
			}

		case FM_IOCTL_GETGOODBCNT:
			{
				FM_DEBUG("FM_IOCTL_GETGOODBCNT\n");
				break;
			}

		case FM_IOCTL_GETBADBNT:
			{
				FM_DEBUG("FM_IOCTL_GETBADBNT\n");
				break;
			}

		case FM_IOCTL_GETBLERRATIO:
			{
				FM_DEBUG("FM_IOCTL_GETBLERRATIO\n");
				break;
			}

		case FM_IOCTL_POWERUP_TX:
			{
				FM_DEBUG("FM_IOCTL_POWERUP_TX\n");
				break;
			}

		case FM_IOCTL_TUNE_TX:
			{
				FM_DEBUG("FM_IOCTL_TUNE_TX\n");
				break;
			}

		case FM_IOCTL_TX_SUPPORT:
			{
				FM_DEBUG("FM_IOCTL_TX_SUPPORT\n");
				break;
			}

		case FM_IOCTL_RDSTX_ENABLE:
			{
				FM_DEBUG("FM_IOCTL_RDSTX_ENABLE\n");
				break;
			}

		case FM_IOCTL_RDSTX_SUPPORT:
			{
				FM_DEBUG("FM_IOCTL_RDSTX_SUPPORT\n");
				break;
			}

		case FM_IOCTL_TX_SCAN:
			{
				FM_DEBUG("FM_IOCTL_TX_SCAN\n");
				break;
			}

		case FM_IOCTL_RDS_TX:
			{
				FM_DEBUG("FM_IOCTL_RDS_TX\n");
				break;
			}

		case FM_IOCTL_OVER_BT_ENABLE:
			{
				FM_DEBUG("FM_IOCTL_OVER_BT_ENABLE\n");
				break;
			}

		case FM_IOCTL_ANA_SWITCH:
			{
				FM_DEBUG("FM_IOCTL_ANA_SWITCH\n");
				break;
			}

		case FM_IOCTL_GETCAPARRAY:
			{
				FM_DEBUG("FM_IOCTL_GETCAPARRAY\n");
				break;
			}

		case FM_IOCTL_GPS_RTC_DRIFT:
			{
				FM_DEBUG("FM_IOCTL_GPS_RTC_DRIFT\n");
				break;
			}

		case FM_IOCTL_I2S_SETTING:
			{
				FM_DEBUG("FM_IOCTL_I2S_SETTING\n");
				break;
			}

		case FM_IOCTL_RDS_GROUPCNT:
			{
				FM_DEBUG("FM_IOCTL_RDS_GROUPCNT\n");
				break;
			}

		case FM_IOCTL_RDS_GET_LOG:
			{
				FM_DEBUG("FM_IOCTL_RDS_GET_LOG\n");
				break;
			}

		case FM_IOCTL_GET_HW_INFO:
			{
				FM_DEBUG("FM_IOCTL_GET_HW_INFO\n");
				break;
			}
		default:
			{
				FM_DEBUG("default\n");
				break;
			}
	}

	return ret;
}
static loff_t fm_ops_lseek(struct file *filp, loff_t off, int whence)
{
//	struct fm *fm = filp->private_data;

	if(whence == SEEK_END){
		//fm_hwscan_stop(fm);
		atomic_set(&scan_complete_flag, 0);
	}else if(whence == SEEK_SET){  
		//FM_EVENT_SEND(fm->rds_event, FM_RDS_DATA_READY);
	}   
	return off;    
}

static int fm_ops_open(struct inode *inode, struct file *filp)
{
	struct fm *fm = container_of(inode->i_cdev, struct fm, cdev);

	FM_DEBUG("%s\n", __func__);

	if (down_interruptible(&fm_ops_mutex))
		return -EFAULT;

	// TODO: only have to set in the first time?
	// YES!!!!

	fm->ref++;

	up(&fm_ops_mutex);

	filp->private_data = fm;

	// TODO: check open flags

	return 0;
}

static int fm_ops_release(struct inode *inode, struct file *filp)
{
	int err = 0;
	struct fm *fm = container_of(inode->i_cdev, struct fm, cdev);

	FM_DEBUG("%s\n", __func__);

	if (down_interruptible(&fm_ops_mutex))
		return -EFAULT;
	fm->ref--;
	if(fm->ref < 1) {
		if(fm->powerup == true) {
			fm_powerdown(fm);           
		}
	}

	up(&fm_ops_mutex);

	return err;
}

static int fm_init(void)
{
	int err;
	struct fm *fm = NULL;
	int ret = -1;


	FM_DEBUG("%s()\n", __func__);
	if (!(fm = kzalloc(sizeof(struct fm), GFP_KERNEL)))
	{
		FM_ALERT("-ENOMEM\n");
		err = -ENOMEM;
		goto ERR_EXIT;
	}

	fm->ref = 0;
	fm->powerup = false;
	fm->chipAddr = RDAFM_SLAVE_ADDR;
	atomic_set(&scan_complete_flag, 0);

	// First, read 5802NM chip ID
	FM_DEBUG("%s()First, read 5802NM chip ID\n", __func__);
	ret = RDAFM_GetChipID(fm->chipAddr, &RDAFM_CHIP_ID);
	FM_DEBUG("%s() 5802NM chip ID = 0x%04x\n", __func__, RDAFM_CHIP_ID);
	// if failed, means use FM in 5990P_E
	if(ret < 0){
		// enable the FM chip in combo
		FM_DEBUG("%s() enable the FM chip in combo\n", __func__);
		ret = rda_fm_power_on();
		if(ret < 0){
			err = -ENOMEM;
			goto ERR_EXIT;
		}
		msleep(100);
		ret = RDAFM_GetChipID(fm->chipAddr, &RDAFM_CHIP_ID);
		FM_DEBUG("%s() the FM in combo chip ID = 0x%04x\n", __func__, RDAFM_CHIP_ID);
		if(ret < 0){
			err = -ENOMEM;
			goto ERR_EXIT;
		}else{
			fm->chip_id = RDAFM_CHIP_ID;
		}

		// disable the FM chip for power saving
		ret = rda_fm_power_off();
		if(ret < 0){
			err = -ENOMEM;
			goto ERR_EXIT;
		}
	}else{
		fm->chip_id = RDAFM_CHIP_ID;
	}



	if ((err = fm_setup_cdev(fm)))
	{
		goto ERR_EXIT;
	}

	g_fm_struct = fm;

	/***********Add porc file system*************/

	g_fm_proc = create_proc_entry(FM_PROC_FILE, 0444, NULL);
	if (g_fm_proc == NULL) {
		FM_ALERT("create_proc_entry failed\n");
		err = -ENOMEM;
		goto ERR_EXIT;
	} else {
		g_fm_proc->read_proc = fm_proc_read;
		g_fm_proc->write_proc = NULL;
		//g_fm_proc->owner = THIS_MODULE;
		FM_ALERT("create_proc_entry success\n");
	}

	/********************************************/

	FM_DEBUG("fm_init is ok!\n");

	return 0;

ERR_EXIT:
	kfree(fm);

	return err;
}

static int fm_proc_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	int cnt= 0;
	struct fm *fm  = g_fm_struct;
	FM_ALERT("Enter fm_proc_read.\n");
	if(off != 0)
		return 0;
	if (fm != NULL && fm->powerup) {
		cnt = sprintf(page, "1\n");
	} else {
		cnt = sprintf(page, "0\n");
	}
	*eof = 1;
	FM_ALERT("Leave fm_proc_read. cnt = %d\n", cnt);
	return cnt;
}


static int fm_destroy(struct fm *fm)
{
	int err = 0;

	FM_DEBUG("%s\n", __func__);

	device_destroy(fm->cls, fm->dev_t);
	class_destroy(fm->cls);

	cdev_del(&fm->cdev);
	unregister_chrdev_region(fm->dev_t, 1);

	fm_powerdown(fm);

	/***********************************/
	remove_proc_entry(FM_PROC_FILE, NULL);

	/**********************************/

	// FIXME: any other hardware configuration ?

	// free all memory
	kfree(fm);

	return err;
}

/*
 *  fm_powerup
 */
static int fm_powerup(struct fm *fm, struct fm_tune_parm *parm)
{
	int i;
	uint16_t tRegValue = 0x0002;
	int ret = -1;


	if (fm->powerup)
	{
		parm->err = FM_BADSTATUS;
		return -EPERM;
	}

	//  if chip_id is ID_RDA5820NS, enable the FM chip in combo
	if(fm->chip_id == ID_RDA5820NS){
		ret = rda_fm_power_on();
		if(ret < 0){
			return -EPERM;
		}
		msleep(100);
	}


	//Reset RDA FM
	tRegValue = 0x0002;
	RDAFM_write(fm->chipAddr, 0x02, tRegValue);
	msleep(100);



	if (ID_RDA5802N == RDAFM_CHIP_ID){
		for (i=0; i<((sizeof(RDA5802N_initialization_reg)) / (sizeof(uint16_t))); i++)
		{
			ret = RDAFM_write(fm->chipAddr, i+2, RDA5802N_initialization_reg[i]);

			if (ret < 0)
			{
				FM_DEBUG("fm_powerup init failed!\n");

				parm->err = FM_FAILED;

				return -EPERM;
			}
		}

	}else if (ID_RDA5820NS == RDAFM_CHIP_ID){
		if(RDA_RADIO_WorkType == FM_RECEIVER){
			for (i = 0; i < ((sizeof(RDA5820NS_RX_initialization_reg)) / (sizeof(RDA_FM_REG_T))); i++)
			{
				if(RDA5820NS_RX_initialization_reg[i].address == 0xFF){
					msleep(RDA5820NS_RX_initialization_reg[i].value);
				}else{
					ret = RDAFM_write(fm->chipAddr, RDA5820NS_RX_initialization_reg[i].address, RDA5820NS_RX_initialization_reg[i].value);
					if (ret < 0)
					{
						FM_DEBUG("fm_powerup init failed!\n");
						parm->err = FM_FAILED;
						return -EPERM;
					}
				}
			}
		}else{
			for (i = 0; i < ((sizeof(RDA5820NS_TX_initialization_reg)) / (sizeof(RDA_FM_REG_T))); i++)
			{
				if(RDA5820NS_TX_initialization_reg[i].address == 0xFF){
					msleep(RDA5820NS_TX_initialization_reg[i].value);
				}else{
					ret = RDAFM_write(fm->chipAddr, RDA5820NS_TX_initialization_reg[i].address, RDA5820NS_TX_initialization_reg[i].value);
					if (ret < 0)
					{
						FM_DEBUG("fm_powerup init failed!\n");
						parm->err = FM_FAILED;
						return -EPERM;
					}
				}
			}
		}

	}


	FM_DEBUG("pwron ok\n");
	fm->powerup = true;

	if (fm_tune(fm, parm) < 0)
	{
		return -EPERM;
	}
	fm_tune_data.byPowerUp = true;
	memcpy(&fm_tune_data.parm, parm, sizeof(fm_tune_data.parm));

	parm->err = FM_SUCCESS;

	return 0;

}

/*
 *  fm_powerdown
 */
static int fm_powerdown(struct fm *fm)
{
	uint16_t tRegValue = 0;
	int ret = -1;

	RDAFM_read(fm->chipAddr, 0x02, &tRegValue);
	tRegValue &= (~(1 << 0));
	RDAFM_write(fm->chipAddr, 0x02, tRegValue);

	if(fm->chip_id == ID_RDA5820NS){
		ret = rda_fm_power_off();
		if(ret < 0){
			return -EPERM;
		}
	}

	fm->powerup = false;
	FM_ALERT("pwrdown ok\n");

	return 0;
}

/*
 *  fm_seek
 */
static int fm_seek(struct fm *fm, struct fm_seek_parm *parm)
{
	int ret = 0;
	uint16_t val = 0;
	uint8_t spaec = 1;
	uint16_t tFreq = 875;
	uint16_t tRegValue = 0;
	uint16_t bottomOfBand = 875;
	int falseStation = -1;


	if (!fm->powerup)
	{
		parm->err = FM_BADSTATUS;
		return -EPERM;
	}

	if (parm->space == FM_SPACE_100K)
	{
		spaec = 1;
		val &= (~((1<<0) | (1<<1)));
	}
	else if (parm->space == FM_SPACE_200K)
	{
		spaec = 2;
		val &= (~(1<<1));
		val |= (1<<0);
	}
	else
	{
		parm->err = FM_EPARM;
		return -EPERM;
	}

	if (parm->band == FM_BAND_UE)
	{
		val &= (~((1<<2) | (1<<3)));
		bottomOfBand = 875;
		fm->min_freq = 875;
		fm->max_freq = 1080;
	}
	else if (parm->band == FM_BAND_JAPAN) 
	{
		val &= (~(1<<3));
		val |= (1 << 2);
		bottomOfBand = 760;
		fm->min_freq = 760;
		fm->max_freq = 910;
	}
	else if (parm->band == FM_BAND_JAPANW) {
		val &= (~(1<<2));
		val |= (1 << 3);
		bottomOfBand = 760;
		fm->min_freq = 760;
		fm->max_freq = 1080;
	}
	else
	{
		FM_ALERT("band:%d out of range\n", parm->band);
		parm->err = FM_EPARM;
		return -EPERM;
	}

	if (parm->freq < fm->min_freq || parm->freq > fm->max_freq) {
		FM_ALERT("freq:%d out of range\n", parm->freq);
		parm->err = FM_EPARM;
		return -EPERM;
	}

	if (parm->seekth > 0x0B) {
		FM_ALERT("seekth:%d out of range\n", parm->seekth);
		parm->err = FM_EPARM;
		return -EPERM;
	}

	RDAFM_read(fm->chipAddr, 0x05, &tRegValue);
	tRegValue &= (~(0x7f<<8));
	//tRegValue |= ((parm->seekth & 0x7f) << 8);
	tRegValue |= ((0x8 & 0x7f) << 8);
	RDAFM_write(fm->chipAddr, 0x05, tRegValue);


#ifdef FMDEBUG
	if (parm->seekdir == FM_SEEK_UP)
		FM_DEBUG("seek %d up\n", parm->freq);
	else
		FM_DEBUG("seek %d down\n", parm->freq);
#endif

	// (1) set hmute bit
	RDAFM_enable_hmute(fm->chipAddr);

	tFreq = parm->freq;

	do {

		if (parm->seekdir == FM_SEEK_UP)
			tFreq += spaec;
		else
			tFreq -= spaec;

		if (tFreq > fm->max_freq)
			tFreq = fm->min_freq;
		if (tFreq < fm->min_freq)
			tFreq = fm->max_freq;

		val = (((tFreq - bottomOfBand+5) << 6) | (1 << 4) | (val & 0x0f));
		RDAFM_write(fm->chipAddr, 0x03, val);
		msleep(40);
		ret = RDAFM_read(fm->chipAddr, 0x0B, &tRegValue);
		if (ret < 0)
		{
			FM_DEBUG("fm_seek: read register failed tunning freq = %4X\n", tFreq);
			falseStation = -1;
		}
		else
		{
			if ((tRegValue & 0x0100) == 0x0100)
				falseStation = 0;
			else
				falseStation = -1;
		}

		if(falseStation == 0)
			break;

	}while(tFreq != parm->freq);


	//clear hmute
	RDAFM_clear_hmute(fm->chipAddr);

	if (falseStation == 0) // seek successfully
	{    
		parm->freq = tFreq;
		FM_ALERT("fm_seek success, freq:%d\n", parm->freq);
		parm->err = FM_SUCCESS;

	}
	else
	{
		FM_ALERT("fm_seek failed, invalid freq\n");
		parm->err = FM_SEEK_FAILED;
		ret = -1;
	}

	return ret;
}

/*
 *  fm_scan
 */
static int  fm_scan(struct fm *fm, struct fm_scan_parm *parm)
{
	int ret = 0;
	uint16_t tRegValue = 0;
	uint16_t scandir = RDA599X_FM_SCAN_UP; //scandir 搜索方向
	uint8_t space = 1; 

	if (!fm->powerup){
		parm->err = FM_BADSTATUS;
		return -EPERM;
	}

	RDAFM_read(fm->chipAddr, 0x03, &tRegValue);

	if (parm->space == FM_SPACE_100K){
		space = 1;
		tRegValue &= (~((1<<0) | (1<<1))); //set 03H's bit[1:0] to 00
	}else if (parm->space == FM_SPACE_200K) {
		space = 2;
		tRegValue &= (~(1<<1)); //clear bit[1]
		tRegValue |= (1<<0);    //set bit[0]
	}else{
		//default
		space = 1;
		tRegValue &= (~((1<<0) | (1<<1))); //set 03H's bit[1:0] to 00
	}

	if(parm->band == FM_BAND_UE){
		tRegValue &= (~((1<<2) | (1<<3)));
		fm->min_freq = 875;
		fm->max_freq = 1080;
	}else if(parm->band == FM_BAND_JAPAN){
		tRegValue &= (~(1<<3));
		tRegValue |= (1 << 2);
		fm->min_freq = 760;
		fm->max_freq = 900;
	}else if(parm->band == FM_BAND_JAPANW){
		tRegValue &= (~(1<<2));
		tRegValue |= (1 << 3);
		fm->min_freq = 760;
		fm->max_freq = 1080;
	}else{
		parm->err = FM_EPARM;
		return -EPERM;
	}

	//set space and band
	RDAFM_write(fm->chipAddr, 0x03, tRegValue);
	msleep(40);


	if(RDAFM_Scan(fm->chipAddr, fm->min_freq, fm->max_freq, &(parm->freq), parm->ScanTBL, &(parm->ScanTBLSize), scandir, space)){
		parm->err = FM_SUCCESS;
	}else{
		parm->err = FM_SEEK_FAILED;
	}

	return ret;
}


static int fm_setvol(struct fm *fm, uint32_t vol)
{
	int ret = 0;
	uint16_t tRegValue = 0;

	if (vol > 15)
		vol = 15;

	FM_DEBUG("fm_setvol:%d\n", vol);

	ret = RDAFM_read(fm->chipAddr, 0x05, &tRegValue);
	if (ret)
		return -EPERM;
	tRegValue &= ~(0x000f);
	tRegValue |= vol;

	ret = RDAFM_write(fm->chipAddr, 0x05, tRegValue);
	if (ret)
		return -EPERM;

	return 0;
}

static int fm_getvol(struct fm *fm, uint32_t *vol)
{
	int ret = 0;
	uint16_t tRegValue;

	ret = RDAFM_read(fm->chipAddr, 0x05, &tRegValue);
	if (ret)
		return -EPERM;

	if (ret)
		return -EPERM;

	*vol = (tRegValue & 0x000F);

	return 0;
}

static int fm_getrssi(struct fm *fm, uint32_t *rssi)
{
	int ret = 0;
	uint16_t tRegValue;

	ret = RDAFM_read(fm->chipAddr, 0x0B, &tRegValue);
	if (ret)
		return -EPERM;


	*rssi = (uint32_t)((tRegValue >> 9) & RDAFM_MASK_RSSI);

	FM_DEBUG("rssi value:%d\n", *rssi);

	return 0;
}

/*
 *  fm_tune
 */
static int fm_tune(struct fm *fm, struct fm_tune_parm *parm)
{
	int ret;
	uint16_t val = 0;
	uint8_t space = 1;
	uint16_t bottomOfBand = 875;


	FM_DEBUG("%s\n", __func__);

	if (!fm->powerup)
	{
		parm->err = FM_BADSTATUS;
		return -EPERM;
	} 

	if (parm->space == FM_SPACE_100K)
	{
		space = 1;
		val &= (~((1<<0) | (1<<1)));
	}
	else if (parm->space == FM_SPACE_200K)
	{
		space = 2;
		val |= (1<<0);
		val &= (~(1<<1));
	}
	else
	{
		parm->err = FM_EPARM;
		return -EPERM;
	}

	if (parm->band == FM_BAND_UE)
	{
		val &= (~((1<<2) | (1<<3)));
		bottomOfBand = 875;
		fm->min_freq = 875;
		fm->max_freq = 1080;
	}
	else if (parm->band == FM_BAND_JAPAN) 
	{
		val &= (~(1<<3));
		val |= (1 << 2);
		bottomOfBand = 760;
		fm->min_freq = 760;
		fm->max_freq = 910;
	}
	else if (parm->band == FM_BAND_JAPANW) {
		val &= (~(1<<2));
		val |= (1 << 3);
		bottomOfBand = 760;
		fm->min_freq = 760;
		fm->max_freq = 1080;
	}
	else
	{
		FM_ALERT("band:%d out of range\n", parm->band);
		parm->err = FM_EPARM;
		return -EPERM;
	}

	if (parm->freq < fm->min_freq || parm->freq > fm->max_freq) {
		FM_ALERT("freq:%d out of range\n", parm->freq);
		parm->err = FM_EPARM;
		return -EPERM;
	}

	FM_DEBUG("fm_tune, freq:%d\n", parm->freq);


	val = (((parm->freq - bottomOfBand + 5) << 6) | (1 << 4) | (val & 0x0f));

	ret = RDAFM_write(fm->chipAddr, 0x03, val);
	if (ret < 0)
	{
		FM_ALERT("fm_tune write freq failed\n");
		parm->err = FM_SEEK_FAILED;
		return ret;
	}
	msleep(40);

	return ret;
}


static int mt_fm_probe(struct platform_device *pdev)
{
	int err = -1;
	FM_DEBUG("mt_fm_probe\n");

	if ((err = fm_init()))
	{
		FM_ALERT("fm_init ERR:%d\n", err);
	}   

	return err;   
} 

static int mt_fm_remove(struct platform_device *pdev)
{
	FM_DEBUG("mt_fm_remove\n");

	struct fm *fm = g_fm_struct;
	if(fm)
	{    
		fm_destroy(fm);
		fm = NULL;
	}

	return 0; 
}


static struct platform_driver mt_fm_dev_drv =
{
	.probe   = mt_fm_probe,
	.remove  = mt_fm_remove,
	.driver = {
		.name   = FM_NAME,
		.owner  = THIS_MODULE,    
	}
};

#if defined(MTK_MT6515)
static struct platform_device mt_fm_device = {
	.name   = FM_NAME,
	.id = -1, 
};
#endif


/*
 *  mt_fm_init
 */
static int __init mt_fm_init(void)
{
	int err = 0;

	FM_DEBUG("mt_fm_init\n");
#if defined(MTK_MT6515)
	err = platform_device_register(&mt_fm_device);
	if(err){
		FM_DEBUG("platform_device_register  fail\n");
		return err;
	}else{
		FM_DEBUG("platform_device_register  success\n");
	}
#endif
	err = platform_driver_register(&mt_fm_dev_drv);
	if (err)
	{
		FM_DEBUG("platform_driver_register failed\n");
	}else{
		FM_DEBUG("platform_driver_register success\n");
	}

	return err;
}

/*
 *  mt_fm_exit
 */
static void __exit mt_fm_exit(void)
{
	FM_DEBUG("mt_fm_exit\n");
	platform_driver_unregister(&mt_fm_dev_drv);
#if defined(MTK_MT6515)
	platform_device_unregister(&mt_fm_device);
#endif
}

module_init(mt_fm_init);
module_exit(mt_fm_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MediaTek FM Driver");
MODULE_AUTHOR("William Chung <William.Chung@MediaTek.com>");
