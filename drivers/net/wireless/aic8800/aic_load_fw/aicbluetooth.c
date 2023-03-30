// SPDX-License-Identifier: GPL-2.0-or-later
#include <linux/version.h>
#include <linux/vmalloc.h>
#include "aicbluetooth_cmds.h"
#include "aicwf_usb.h"
#include "md5.h"
#ifdef CONFIG_USE_FW_REQUEST
#include <linux/firmware.h>
#endif

//Parser state
#define INIT 0
#define CMD 1
#define PRINT 2
#define GET_VALUE 3

typedef struct
{
    int8_t enable;
    int8_t dsss;
    int8_t ofdmlowrate_2g4;
    int8_t ofdm64qam_2g4;
    int8_t ofdm256qam_2g4;
    int8_t ofdm1024qam_2g4;
    int8_t ofdmlowrate_5g;
    int8_t ofdm64qam_5g;
    int8_t ofdm256qam_5g;
    int8_t ofdm1024qam_5g;
} txpwr_idx_conf_t;


txpwr_idx_conf_t userconfig_txpwr_idx = {
	.enable 		  = 1,
	.dsss			  = 9,
	.ofdmlowrate_2g4  = 8,
	.ofdm64qam_2g4	  = 8,
	.ofdm256qam_2g4   = 8,
	.ofdm1024qam_2g4  = 8,
	.ofdmlowrate_5g   = 11,
	.ofdm64qam_5g	  = 10,
	.ofdm256qam_5g	  = 9,
	.ofdm1024qam_5g   = 9

};

typedef struct
{
    int8_t enable;
    int8_t chan_1_4;
    int8_t chan_5_9;
    int8_t chan_10_13;
    int8_t chan_36_64;
    int8_t chan_100_120;
    int8_t chan_122_140;
    int8_t chan_142_165;
} txpwr_ofst_conf_t;

txpwr_ofst_conf_t userconfig_txpwr_ofst = {
	.enable = 1,
	.chan_1_4 = 0,
	.chan_5_9 = 0,
	.chan_10_13 = 0,
	.chan_36_64 = 0,
	.chan_100_120 = 0,
	.chan_122_140 = 0,
	.chan_142_165 = 0
};

typedef struct
{
    int8_t enable;
    int8_t xtal_cap;
    int8_t xtal_cap_fine;
} xtal_cap_conf_t;


xtal_cap_conf_t userconfig_xtal_cap = {
	.enable = 0,
	.xtal_cap = 24,
	.xtal_cap_fine = 31,
};


struct aicbt_patch_table {
	char     *name;
	uint32_t type;
	uint32_t *data;
	uint32_t len;
	struct aicbt_patch_table *next;
};

struct aicbt_info_t {
    uint32_t btmode;
    uint32_t btport;
    uint32_t uart_baud;
    uint32_t uart_flowctrl;
	uint32_t lpm_enable;
	uint32_t txpwr_lvl;
};

struct aicbsp_info_t {
    int hwinfo;
    uint32_t cpmode;
};

#define AICBT_PT_TAG          "AICBT_PT_TAG"
#define AICBT_PT_TRAP         0x01
#define AICBT_PT_B4           0x02
#define AICBT_PT_BTMODE       0x03
#define AICBT_PT_PWRON        0x04
#define AICBT_PT_AF           0x05

enum aicbt_btport_type {
    AICBT_BTPORT_NULL,
    AICBT_BTPORT_MB,
    AICBT_BTPORT_UART,
};

/*  btmode
 * used for force bt mode,if not AICBSP_MODE_NULL
 * efuse valid and vendor_info will be invalid, even has beed set valid
*/
enum aicbt_btmode_type {
    AICBT_BTMODE_BT_ONLY_SW = 0x0,    // bt only mode with switch
    AICBT_BTMODE_BT_WIFI_COMBO,       // wifi/bt combo mode
    AICBT_BTMODE_BT_ONLY,             // bt only mode without switch
    AICBT_BTMODE_BT_ONLY_TEST,        // bt only test mode
    AICBT_BTMODE_BT_WIFI_COMBO_TEST,  // wifi/bt combo test mode
    AICBT_MODE_NULL = 0xFF,           // invalid value
};

/*  uart_baud
 * used for config uart baud when btport set to uart,
 * otherwise meaningless
*/
enum aicbt_uart_baud_type {
    AICBT_UART_BAUD_115200     = 115200,
    AICBT_UART_BAUD_921600     = 921600,
    AICBT_UART_BAUD_1_5M       = 1500000,
    AICBT_UART_BAUD_3_25M      = 3250000,
};

enum aicbt_uart_flowctrl_type {
    AICBT_UART_FLOWCTRL_DISABLE = 0x0,    // uart without flow ctrl
    AICBT_UART_FLOWCTRL_ENABLE,           // uart with flow ctrl
};

enum aicbsp_cpmode_type {
    AICBSP_CPMODE_WORK,
    AICBSP_CPMODE_TEST,
};
#define AIC_M2D_OTA_INFO_ADDR       0x88000020
#define AIC_M2D_OTA_DATA_ADDR       0x88000040
#define AIC_M2D_OTA_FLASH_ADDR      0x08004000
#define AIC_M2D_OTA_CODE_START_ADDR 0x08004188
#define AIC_M2D_OTA_VER_ADDR        0x0800418c
///aic bt tx pwr lvl :lsb->msb: first byte, min pwr lvl; second byte, max pwr lvl;
///pwr lvl:20(min), 30 , 40 , 50 , 60(max)
#define AICBT_TXPWR_LVL            0x00006020

#define AICBSP_MODE_BT_HCI_MODE_NULL              0
#define AICBSP_MODE_BT_HCI_MODE_MB                1
#define AICBSP_MODE_BT_HCI_MODE_UART              2

#define AICBSP_HWINFO_DEFAULT       (-1)
#define AICBSP_CPMODE_DEFAULT       AICBSP_CPMODE_WORK

#define AICBT_BTMODE_DEFAULT        AICBT_BTMODE_BT_ONLY
#define AICBT_BTPORT_DEFAULT        AICBT_BTPORT_MB
#define AICBT_UART_BAUD_DEFAULT     AICBT_UART_BAUD_1_5M
#define AICBT_UART_FC_DEFAULT       AICBT_UART_FLOWCTRL_ENABLE
#define AICBT_LPM_ENABLE_DEFAULT    0
#define AICBT_TXPWR_LVL_DEFAULT     AICBT_TXPWR_LVL

#define AIC_HW_INFO 0x21

#define FW_PATH_MAX 200
#if defined(CONFIG_PLATFORM_UBUNTU)
static const char* aic_default_fw_path = "/lib/firmware/";
#else
static const char* aic_default_fw_path = "/vendor/etc/firmware";
#endif
char aic_fw_path[FW_PATH_MAX];
module_param_string(aic_fw_path, aic_fw_path, FW_PATH_MAX, 0660);
#ifdef CONFIG_M2D_OTA_AUTO_SUPPORT
char saved_sdk_ver[64];
module_param_string(saved_sdk_ver, saved_sdk_ver,64, 0660);
#endif


int aic_bt_platform_init(struct aic_usb_dev *usbdev)
{
    rwnx_cmd_mgr_init(&usbdev->cmd_mgr);
    usbdev->cmd_mgr.usbdev = (void *)usbdev;
    return 0;

}

void aic_bt_platform_deinit(struct aic_usb_dev *usbdev)
{

}

#define MD5(x) x[0],x[1],x[2],x[3],x[4],x[5],x[6],x[7],x[8],x[9],x[10],x[11],x[12],x[13],x[14],x[15]
#define MD5PINRT "file md5:%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\r\n"

static int aic_load_firmware(u32 ** fw_buf, const char *name, struct device *device)
{

#ifdef CONFIG_USE_FW_REQUEST
	const struct firmware *fw = NULL;
	u32 *dst = NULL;
	void *buffer=NULL;
	MD5_CTX md5;
	unsigned char decrypt[16];
	int size = 0;
	int ret = 0;

	printk("%s: request firmware = %s \n", __func__ ,name);


	ret = request_firmware(&fw, name, NULL);
	
	if (ret < 0) {
		printk("Load %s fail\n", name);
		release_firmware(fw);
		return -1;
	}
	
	size = fw->size;
	dst = (u32 *)fw->data;

	if (size <= 0) {
		printk("wrong size of firmware file\n");
		release_firmware(fw);
		return -1;
	}


	buffer = vmalloc(size);
	memset(buffer, 0, size);
	memcpy(buffer, dst, size);
	
	*fw_buf = buffer;

	MD5Init(&md5);
	MD5Update(&md5, (unsigned char *)buffer, size);
	MD5Final(&md5, decrypt);
	printk(MD5PINRT, MD5(decrypt));
	
	release_firmware(fw);
	
	return size;
#else
    void *buffer=NULL;
    char *path=NULL;
    struct file *fp=NULL;
    int size = 0, len=0, i=0;
    ssize_t rdlen=0;
    u32 *src=NULL, *dst = NULL;
	MD5_CTX md5;
	unsigned char decrypt[16];

    /* get the firmware path */
    path = __getname();
    if (!path){
            *fw_buf=NULL;
            return -1;
    }

    if (strlen(aic_fw_path) > 0) {
		printk("%s: use customer define fw_path\n", __func__);
		len = snprintf(path, FW_PATH_MAX, "%s/%s", aic_fw_path, name);
    } else {
    #if defined(CONFIG_PLATFORM_UBUNTU)
		len = snprintf(path, FW_PATH_MAX, "%s/%s/%s",aic_default_fw_path, "aic8800", name);
	#else
		len = snprintf(path, FW_PATH_MAX, "%s/%s",aic_default_fw_path, name);
	#endif
    }

    if (len >= FW_PATH_MAX) {
    	printk("%s: %s file's path too long\n", __func__, name);
        *fw_buf=NULL;
        __putname(path);
        return -1;
    }

    printk("%s :firmware path = %s  \n", __func__ ,path);


    /* open the firmware file */
    fp=filp_open(path, O_RDONLY, 0);
    if(IS_ERR(fp) || (!fp)){
            printk("%s: %s file failed to open\n", __func__, name);
            if(IS_ERR(fp))
		printk("is_Err\n");
	if((!fp))
		printk("null\n");
	*fw_buf=NULL;
            __putname(path);
            fp=NULL;
            return -1;
    }

    size = i_size_read(file_inode(fp));
    if(size<=0){
            printk("%s: %s file size invalid %d\n", __func__, name, size);
            *fw_buf=NULL;
            __putname(path);
            filp_close(fp,NULL);
            fp=NULL;
            return -1;
}

    /* start to read from firmware file */
    buffer = vmalloc(size);
    memset(buffer, 0, size);
    if(!buffer){
            *fw_buf=NULL;
            __putname(path);
            filp_close(fp,NULL);
            fp=NULL;
            return -1;
    }


    #if LINUX_VERSION_CODE > KERNEL_VERSION(4, 13, 16)
    rdlen = kernel_read(fp, buffer, size, &fp->f_pos);
    #else
    rdlen = kernel_read(fp, fp->f_pos, buffer, size);
    #endif

    if(size != rdlen){
            printk("%s: %s file rdlen invalid %d %d\n", __func__, name, (int)rdlen, size);
            *fw_buf=NULL;
            __putname(path);
            filp_close(fp,NULL);
            fp=NULL;
            vfree(buffer);
            buffer=NULL;
            return -1;
    }
    if(rdlen > 0){
            fp->f_pos += rdlen;
            //printk("f_pos=%d\n", (int)fp->f_pos);
    }


   /*start to transform the data format*/
    src = (u32*)buffer;
    //printk("malloc dst\n");
    dst = (u32*)vmalloc(size);
    memset(dst, 0, size);

    if(!dst){
            *fw_buf=NULL;
            __putname(path);
            filp_close(fp,NULL);
            fp=NULL;
            vfree(buffer);
            buffer=NULL;
            return -1;
    }

    for(i=0;i<(size/4);i++){
            dst[i] = src[i];
    }

    __putname(path);
    filp_close(fp,NULL);
    fp=NULL;
    vfree(buffer);
    buffer=NULL;
    *fw_buf = dst;

	MD5Init(&md5);
	MD5Update(&md5, (unsigned char *)dst, size);
	MD5Final(&md5, decrypt);

	printk(MD5PINRT, MD5(decrypt));

    return size;
#endif

}

int rwnx_plat_bin_fw_upload_android(struct aic_usb_dev *usbdev, u32 fw_addr,
                               char *filename)
{
    struct device *dev = usbdev->dev;
    unsigned int i=0;
    int size;
    u32 *dst=NULL;
    int err=0;

    /* load aic firmware */
    size = aic_load_firmware(&dst, filename, dev);
    if(size<=0){
            printk("wrong size of firmware file\n");
            vfree(dst);
            dst = NULL;
            return -1;
    }

    /* Copy the file on the Embedded side */
    printk("### Upload %s firmware, @ = %x  size=%d\n", filename, fw_addr, size);

    if (size > 1024) {// > 1KB data
        for (i = 0; i < (size - 1024); i += 1024) {//each time write 1KB
            err = rwnx_send_dbg_mem_block_write_req(usbdev, fw_addr + i, 1024, dst + i / 4);
                if (err) {
                printk("bin upload fail: %x, err:%d\r\n", fw_addr + i, err);
                break;
            }
        }
    }

    if (!err && (i < size)) {// <1KB data
        err = rwnx_send_dbg_mem_block_write_req(usbdev, fw_addr + i, size - i, dst + i / 4);
        if (err) {
            printk("bin upload fail: %x, err:%d\r\n", fw_addr + i, err);
        }
    }

    if (dst) {
        vfree(dst);
        dst = NULL;
    }

    printk("fw download complete\n\n");

    return err;
}

extern int testmode;
#ifdef CONFIG_M2D_OTA_AUTO_SUPPORT
int rwnx_plat_m2d_flash_ota_android(struct aic_usb_dev *usbdev, char *filename)
{
    struct device *dev = usbdev->dev;
    unsigned int i=0;
    int size;
    u32 *dst=NULL;
    int err=0;
	int ret;
	u8 bond_id;
    const u32 mem_addr = 0x40500000;
    struct dbg_mem_read_cfm rd_mem_addr_cfm;

    ret = rwnx_send_dbg_mem_read_req(usbdev, mem_addr, &rd_mem_addr_cfm);
    if (ret) {
        printk("m2d %x rd fail: %d\n", mem_addr, ret);
        return ret;
    }
    bond_id = (u8)(rd_mem_addr_cfm.memdata >> 24);
    printk("%x=%x\n", rd_mem_addr_cfm.memaddr, rd_mem_addr_cfm.memdata);
	if (bond_id & (1<<1)) {
		//flash is invalid
		printk("m2d flash is invalid\n");
		return -1;
	}

    /* load aic firmware */
    size = aic_load_firmware(&dst, filename, dev);
    if(size<=0){
            printk("wrong size of m2d file\n");
            vfree(dst);
            dst = NULL;
            return -1;
    }

    /* Copy the file on the Embedded side */
    printk("### Upload m2d %s flash, size=%d\n", filename, size);

	/*send info first*/
	err = rwnx_send_dbg_mem_block_write_req(usbdev, AIC_M2D_OTA_INFO_ADDR, 4, (u32 *)&size);
	
	/*send data first*/
    if (size > 1024) {// > 1KB data
        for (i = 0; i < (size - 1024); i += 1024) {//each time write 1KB
            err = rwnx_send_dbg_mem_block_write_req(usbdev, AIC_M2D_OTA_DATA_ADDR, 1024, dst + i / 4);
                if (err) {
                printk("m2d upload fail: %x, err:%d\r\n", AIC_M2D_OTA_DATA_ADDR, err);
                break;
            }
        }
    }

    if (!err && (i < size)) {// <1KB data
        err = rwnx_send_dbg_mem_block_write_req(usbdev, AIC_M2D_OTA_DATA_ADDR, size - i, dst + i / 4);
        if (err) {
            printk("m2d upload fail: %x, err:%d\r\n", AIC_M2D_OTA_DATA_ADDR, err);
        }
    }

    if (dst) {
        vfree(dst);
        dst = NULL;
    }
	testmode = FW_NORMAL_MODE;

    printk("m2d flash update complete\n\n");

    return err;
}

int rwnx_plat_m2d_flash_ota_check(struct aic_usb_dev *usbdev, char *filename)
{
    struct device *dev = usbdev->dev;
    unsigned int i=0,j=0;
    int size;
    u32 *dst=NULL;
    int err=0;
	int ret=0;
	u8 bond_id;
    const u32 mem_addr = 0x40500000;
	const u32 mem_addr_code_start = AIC_M2D_OTA_CODE_START_ADDR;
	const u32 mem_addr_sdk_ver = AIC_M2D_OTA_VER_ADDR;
	const u32 driver_code_start_idx = (AIC_M2D_OTA_CODE_START_ADDR-AIC_M2D_OTA_FLASH_ADDR)/4;
	const u32 driver_sdk_ver_idx = (AIC_M2D_OTA_VER_ADDR-AIC_M2D_OTA_FLASH_ADDR)/4;
	u32 driver_sdk_ver_addr_idx = 0;
	u32 code_start_addr = 0xffffffff;
	u32 sdk_ver_addr = 0xffffffff;
	u32 drv_code_start_addr = 0xffffffff;
	u32 drv_sdk_ver_addr = 0xffffffff;
    struct dbg_mem_read_cfm rd_mem_addr_cfm;
	char m2d_sdk_ver[64];
	char flash_sdk_ver[64];
	u32 flash_ver[16];
	u32 ota_ver[16];

    ret = rwnx_send_dbg_mem_read_req(usbdev, mem_addr, &rd_mem_addr_cfm);
    if (ret) {
        printk("m2d %x rd fail: %d\n", mem_addr, ret);
        return ret;
    }
    bond_id = (u8)(rd_mem_addr_cfm.memdata >> 24);
    printk("%x=%x\n", rd_mem_addr_cfm.memaddr, rd_mem_addr_cfm.memdata);
	if (bond_id & (1<<1)) {
		//flash is invalid
		printk("m2d flash is invalid\n");
		return -1;
	}
    ret = rwnx_send_dbg_mem_read_req(usbdev, mem_addr_code_start, &rd_mem_addr_cfm);
	if (ret){
        printk("mem_addr_code_start %x rd fail: %d\n", mem_addr_code_start, ret);
        return ret;
	}
	code_start_addr = rd_mem_addr_cfm.memdata;

    ret = rwnx_send_dbg_mem_read_req(usbdev, mem_addr_sdk_ver, &rd_mem_addr_cfm);
	if (ret){
        printk("mem_addr_sdk_ver %x rd fail: %d\n", mem_addr_code_start, ret);
        return ret;
	}
	sdk_ver_addr = rd_mem_addr_cfm.memdata;
	printk("code_start_addr: 0x%x,  sdk_ver_addr: 0x%x\n", code_start_addr,sdk_ver_addr);

	/* load aic firmware */
	size = aic_load_firmware(&dst, filename, dev);
	if(size<=0){
			printk("wrong size of m2d file\n");
			vfree(dst);
			dst = NULL;
			return -1;
	}
	if(code_start_addr == 0xffffffff && sdk_ver_addr == 0xffffffff) {
		printk("########m2d flash old version , must be upgrade\n");
		drv_code_start_addr = dst[driver_code_start_idx];
		drv_sdk_ver_addr = dst[driver_sdk_ver_idx];

		printk("drv_code_start_addr: 0x%x,	drv_sdk_ver_addr: 0x%x\n", drv_code_start_addr,drv_sdk_ver_addr);

		if(drv_sdk_ver_addr == 0xffffffff){
			printk("########driver m2d_ota.bin is old ,not need upgrade\n");
			return -1;
		}

	} else {
		for(i=0;i<16;i++){
			ret = rwnx_send_dbg_mem_read_req(usbdev, (sdk_ver_addr+i*4), &rd_mem_addr_cfm);
			if (ret){
				printk("mem_addr_sdk_ver %x rd fail: %d\n", mem_addr_code_start, ret);
				return ret;
			}
			flash_ver[i] = rd_mem_addr_cfm.memdata;
		}
		memcpy((u8 *)flash_sdk_ver,(u8 *)flash_ver,64);
        memcpy((u8 *)saved_sdk_ver,(u8 *)flash_sdk_ver,64);
		printk("flash SDK Version: %s\r\n\r\n", flash_sdk_ver);
				
		drv_code_start_addr = dst[driver_code_start_idx];
		drv_sdk_ver_addr = dst[driver_sdk_ver_idx];

		printk("drv_code_start_addr: 0x%x,	drv_sdk_ver_addr: 0x%x\n", drv_code_start_addr,drv_sdk_ver_addr);

		if(drv_sdk_ver_addr == 0xffffffff){
			printk("########driver m2d_ota.bin is old ,not need upgrade\n");
			return -1;
		}

		driver_sdk_ver_addr_idx = (drv_sdk_ver_addr-drv_code_start_addr)/4;
		printk("driver_sdk_ver_addr_idx %d\n",driver_sdk_ver_addr_idx);

		if (driver_sdk_ver_addr_idx){
			for(j = 0; j < 16; j++){
				ota_ver[j] = dst[driver_sdk_ver_addr_idx+j];
			}
			memcpy((u8 *)m2d_sdk_ver,(u8 *)ota_ver,64);
			printk("m2d_ota SDK Version: %s\r\n\r\n", m2d_sdk_ver);
		} else {
			return -1;
		}
		
		if(!strcmp(m2d_sdk_ver,flash_sdk_ver)){
			printk("######## m2d %s flash is not need upgrade\r\n", filename);
			return -1;
		}
	}

    /* Copy the file on the Embedded side */
    printk("### Upload m2d %s flash, size=%d\n", filename, size);

	/*send info first*/
	err = rwnx_send_dbg_mem_block_write_req(usbdev, AIC_M2D_OTA_INFO_ADDR, 4, (u32 *)&size);
	
	/*send data first*/
    if (size > 1024) {// > 1KB data
        for (i = 0; i < (size - 1024); i += 1024) {//each time write 1KB
            err = rwnx_send_dbg_mem_block_write_req(usbdev, AIC_M2D_OTA_DATA_ADDR, 1024, dst + i / 4);
                if (err) {
                printk("m2d upload fail: %x, err:%d\r\n", AIC_M2D_OTA_DATA_ADDR, err);
                break;
            }
        }
    }

    if (!err && (i < size)) {// <1KB data
        err = rwnx_send_dbg_mem_block_write_req(usbdev, AIC_M2D_OTA_DATA_ADDR, size - i, dst + i / 4);
        if (err) {
            printk("m2d upload fail: %x, err:%d\r\n", AIC_M2D_OTA_DATA_ADDR, err);
        }
    }

    if (dst) {
        vfree(dst);
        dst = NULL;
    }
	testmode = FW_NORMAL_MODE;

    printk("m2d flash update complete\n\n");

    return err;
}
#endif//CONFIG_M2D_OTA_AUTO_SUPPORT

uint32_t rwnx_atoli(char *value){
	int len = 0;
	int temp_len = 0;
	int i = 0;
	uint32_t result = 0;
	
	temp_len = strlen(value);

	for(i = 0;i < temp_len; i++){
		if((value[i] >= 48 && value[i] <= 57) ||
			(value[i] >= 65 && value[i] <= 70) ||
			(value[i] >= 97 && value[i] <= 102)){
			len++;
		}
	}

	//printk("%s len:%d \r\n", __func__, len);
	
	for(i = 0; i < len; i++){
		result = result * 16;
		if(value[i] >= 48 && value[i] <= 57){
			result += value[i] - 48;
		}else if(value[i] >= 65 && value[i] <= 70){
			result += (value[i] - 65) + 10;
		}else if(value[i] >= 97 && value[i] <= 102){
			result += (value[i] - 97) + 10;
		}
	}
	
	return result;
}

int8_t rwnx_atoi(char *value){
	int len = 0;
	int i = 0;
	int8_t result = 0;
	int8_t signal = 1;

	len = strlen(value);
	//printk("%s len:%d \r\n", __func__, len);

	for(i = 0;i < len ;i++){
		if(i == 0 && value[0] == '-'){
			signal = -1;
			continue;
		}

		result = result * 10;
		if(value[i] >= 48 && value[i] <= 57){
			result += value[i] - 48;
		}else{
			result = 0;
			break;
		}
	}

	result = result * signal;
	//printk("%s result:%d \r\n", __func__, result);

	return result;
}

void get_fw_path(char* fw_path){
	if (strlen(aic_fw_path) > 0) {
		memcpy(fw_path, aic_fw_path, strlen(aic_fw_path));
	}else{
		memcpy(fw_path, aic_default_fw_path, strlen(aic_default_fw_path));
	}
} 

void set_testmode(int val){
	testmode = val;
}

int get_testmode(void){
	return testmode;
}

int get_hardware_info(void){
	return AIC_HW_INFO;
}
EXPORT_SYMBOL(get_fw_path);

EXPORT_SYMBOL(get_testmode);

EXPORT_SYMBOL(set_testmode);


EXPORT_SYMBOL(get_hardware_info);

void get_userconfig_xtal_cap(xtal_cap_conf_t *xtal_cap)
{
	xtal_cap->enable = userconfig_xtal_cap.enable;
	xtal_cap->xtal_cap = userconfig_xtal_cap.xtal_cap;
	xtal_cap->xtal_cap_fine = userconfig_xtal_cap.xtal_cap_fine;

    printk("%s:enable       :%d\r\n", __func__, xtal_cap->enable);
    printk("%s:xtal_cap     :%d\r\n", __func__, xtal_cap->xtal_cap);
    printk("%s:xtal_cap_fine:%d\r\n", __func__, xtal_cap->xtal_cap_fine);
}

EXPORT_SYMBOL(get_userconfig_xtal_cap);

void get_userconfig_txpwr_idx(txpwr_idx_conf_t *txpwr_idx){
	txpwr_idx->enable = userconfig_txpwr_idx.enable;
	txpwr_idx->dsss = userconfig_txpwr_idx.dsss;
	txpwr_idx->ofdmlowrate_2g4 = userconfig_txpwr_idx.ofdmlowrate_2g4;
	txpwr_idx->ofdm64qam_2g4 = userconfig_txpwr_idx.ofdm64qam_2g4;
	txpwr_idx->ofdm256qam_2g4 = userconfig_txpwr_idx.ofdm256qam_2g4;
	txpwr_idx->ofdm1024qam_2g4 = userconfig_txpwr_idx.ofdm1024qam_2g4;
	txpwr_idx->ofdmlowrate_5g = userconfig_txpwr_idx.ofdmlowrate_5g;
	txpwr_idx->ofdm64qam_5g = userconfig_txpwr_idx.ofdm64qam_5g;
	txpwr_idx->ofdm256qam_5g = userconfig_txpwr_idx.ofdm256qam_5g;
	txpwr_idx->ofdm1024qam_5g = userconfig_txpwr_idx.ofdm1024qam_5g;

	printk("%s:enable:%d\r\n", __func__, txpwr_idx->enable);
	printk("%s:dsss:%d\r\n", __func__, txpwr_idx->dsss);
	printk("%s:ofdmlowrate_2g4:%d\r\n", __func__, txpwr_idx->ofdmlowrate_2g4);
	printk("%s:ofdm64qam_2g4:%d\r\n", __func__, txpwr_idx->ofdm64qam_2g4);
	printk("%s:ofdm256qam_2g4:%d\r\n", __func__, txpwr_idx->ofdm256qam_2g4);
	printk("%s:ofdm1024qam_2g4:%d\r\n", __func__, txpwr_idx->ofdm1024qam_2g4);
	printk("%s:ofdmlowrate_5g:%d\r\n", __func__, txpwr_idx->ofdmlowrate_5g);
	printk("%s:ofdm64qam_5g:%d\r\n", __func__, txpwr_idx->ofdm64qam_5g);
	printk("%s:ofdm256qam_5g:%d\r\n", __func__, txpwr_idx->ofdm256qam_5g);
	printk("%s:ofdm1024qam_5g:%d\r\n", __func__, txpwr_idx->ofdm1024qam_5g);

}

EXPORT_SYMBOL(get_userconfig_txpwr_idx);

void get_userconfig_txpwr_ofst(txpwr_ofst_conf_t *txpwr_ofst){
	txpwr_ofst->enable = userconfig_txpwr_ofst.enable;
	txpwr_ofst->chan_1_4 = userconfig_txpwr_ofst.chan_1_4;
	txpwr_ofst->chan_5_9 = userconfig_txpwr_ofst.chan_5_9;
	txpwr_ofst->chan_10_13 = userconfig_txpwr_ofst.chan_10_13;
	txpwr_ofst->chan_36_64 = userconfig_txpwr_ofst.chan_36_64;
	txpwr_ofst->chan_100_120 = userconfig_txpwr_ofst.chan_100_120;
	txpwr_ofst->chan_122_140 = userconfig_txpwr_ofst.chan_122_140;
	txpwr_ofst->chan_142_165 = userconfig_txpwr_ofst.chan_142_165;

	printk("%s:ofst_enable:%d\r\n", __func__, txpwr_ofst->enable);
	printk("%s:ofst_chan_1_4:%d\r\n", __func__, txpwr_ofst->chan_1_4);
	printk("%s:ofst_chan_5_9:%d\r\n", __func__, txpwr_ofst->chan_5_9);
	printk("%s:ofst_chan_10_13:%d\r\n", __func__, txpwr_ofst->chan_10_13);
	printk("%s:ofst_chan_36_64:%d\r\n", __func__, txpwr_ofst->chan_36_64);
	printk("%s:ofst_chan_100_120:%d\r\n", __func__, txpwr_ofst->chan_100_120);
	printk("%s:ofst_chan_122_140:%d\r\n", __func__, txpwr_ofst->chan_122_140);
	printk("%s:ofst_chan_142_165:%d\r\n", __func__, txpwr_ofst->chan_142_165);

}

EXPORT_SYMBOL(get_userconfig_txpwr_ofst);

void rwnx_plat_userconfig_set_value(char *command, char *value){	
	//TODO send command
	printk("%s:command=%s value=%s \r\n", __func__, command, value);
	if(!strcmp(command, "enable")){
		userconfig_txpwr_idx.enable = rwnx_atoi(value);
	}else if(!strcmp(command, "dsss")){
		userconfig_txpwr_idx.dsss = rwnx_atoi(value);
	}else if(!strcmp(command, "ofdmlowrate_2g4")){
		userconfig_txpwr_idx.ofdmlowrate_2g4 = rwnx_atoi(value);
	}else if(!strcmp(command, "ofdm64qam_2g4")){
		userconfig_txpwr_idx.ofdm64qam_2g4 = rwnx_atoi(value);
	}else if(!strcmp(command, "ofdm256qam_2g4")){
		userconfig_txpwr_idx.ofdm256qam_2g4 = rwnx_atoi(value);
	}else if(!strcmp(command, "ofdm1024qam_2g4")){
		userconfig_txpwr_idx.ofdm1024qam_2g4 = rwnx_atoi(value);
	}else if(!strcmp(command, "ofdmlowrate_5g")){
		userconfig_txpwr_idx.ofdmlowrate_5g = rwnx_atoi(value);
	}else if(!strcmp(command, "ofdm64qam_5g")){
		userconfig_txpwr_idx.ofdm64qam_5g = rwnx_atoi(value);
	}else if(!strcmp(command, "ofdm256qam_5g")){
		userconfig_txpwr_idx.ofdm256qam_5g = rwnx_atoi(value);
	}else if(!strcmp(command, "ofdm1024qam_5g")){
		userconfig_txpwr_idx.ofdm1024qam_5g = rwnx_atoi(value);
	}else if(!strcmp(command, "ofst_enable")){
		userconfig_txpwr_ofst.enable = rwnx_atoi(value);
	}else if(!strcmp(command, "ofst_chan_1_4")){
		userconfig_txpwr_ofst.chan_1_4 = rwnx_atoi(value);
	}else if(!strcmp(command, "ofst_chan_5_9")){
		userconfig_txpwr_ofst.chan_5_9 = rwnx_atoi(value);
	}else if(!strcmp(command, "ofst_chan_10_13")){
		userconfig_txpwr_ofst.chan_10_13 = rwnx_atoi(value);
	}else if(!strcmp(command, "ofst_chan_36_64")){
		userconfig_txpwr_ofst.chan_36_64 = rwnx_atoi(value);
	}else if(!strcmp(command, "ofst_chan_100_120")){
		userconfig_txpwr_ofst.chan_100_120 = rwnx_atoi(value);
	}else if(!strcmp(command, "ofst_chan_122_140")){
		userconfig_txpwr_ofst.chan_122_140 = rwnx_atoi(value);
	}else if(!strcmp(command, "ofst_chan_142_165")){
		userconfig_txpwr_ofst.chan_142_165 = rwnx_atoi(value);
	}else if(!strcmp(command, "xtal_enable")){
		userconfig_xtal_cap.enable = rwnx_atoi(value);
	}else if(!strcmp(command, "xtal_cap")){
		userconfig_xtal_cap.xtal_cap = rwnx_atoi(value);
	}else if(!strcmp(command, "xtal_cap_fine")){
		userconfig_xtal_cap.xtal_cap_fine = rwnx_atoi(value);
	}
}

void rwnx_plat_userconfig_parsing(char *buffer, int size){
    int i = 0;
	int parse_state = 0;
	char command[30];
	char value[100];
	int char_counter = 0;

	memset(command, 0, 30);
	memset(value, 0, 100);

    for(i = 0; i < size; i++){

		//Send command or print nvram log when char is \r or \n
		if(buffer[i] == 0x0a || buffer[i] == 0x0d){
			if(command[0] != 0 && value[0] != 0){
				if(parse_state == PRINT){
					printk("%s:%s\r\n", __func__, value);
				}else if(parse_state == GET_VALUE){
					rwnx_plat_userconfig_set_value(command, value);
				}
			}
			//Reset command value and char_counter
			memset(command, 0, 30);
			memset(value, 0, 100);
			char_counter = 0;
			parse_state = INIT;
			continue;
		}

		//Switch parser state
		if(parse_state == INIT){
			if(buffer[i] == '#'){
				parse_state = PRINT;
				continue;
			}else if(buffer[i] == 0x0a || buffer[i] == 0x0d){
				parse_state = INIT;
				continue;
			}else{
				parse_state = CMD;
			}
		}

		//Fill data to command and value
		if(parse_state == PRINT){
			command[0] = 0x01;
			value[char_counter] = buffer[i];
			char_counter++;
		}else if(parse_state == CMD){
			if(command[0] != 0 && buffer[i] == '='){
				parse_state = GET_VALUE;
				char_counter = 0;
				continue;
			}
			command[char_counter] = buffer[i];
			char_counter++;
		}else if(parse_state == GET_VALUE){
			value[char_counter] = buffer[i];
			char_counter++;
		}
	}


}

int rwnx_plat_userconfig_upload_android(char *filename){
    int size;
    u32 *dst=NULL;

	printk("userconfig file path:%s \r\n", filename);

    /* load aic firmware */
    size = aic_load_firmware(&dst, filename, NULL);
    if(size <= 0){
            printk("wrong size of firmware file\n");
            vfree(dst);
            dst = NULL;
            return 0;
    }

	/* Copy the file on the Embedded side */
    printk("### Upload %s userconfig, size=%d\n", filename, size);

	rwnx_plat_userconfig_parsing((char *)dst, size);

	if (dst) {
        vfree(dst);
        dst = NULL;
    }

	printk("userconfig download complete\n\n");
	return 0;
}



int aicbt_patch_table_free(struct aicbt_patch_table **head)
{
	struct aicbt_patch_table *p = *head, *n = NULL;
	while (p) {
		n = p->next;
		vfree(p->name);
		vfree(p->data);
		vfree(p);
		p = n;
	}
	*head = NULL;
	return 0;
}

struct aicbsp_info_t aicbsp_info = {
    .hwinfo   = AICBSP_HWINFO_DEFAULT,
    .cpmode   = AICBSP_CPMODE_DEFAULT,
};

static struct aicbt_info_t aicbt_info = {
    .btmode        = AICBT_BTMODE_DEFAULT,
    .btport        = AICBT_BTPORT_DEFAULT,
    .uart_baud     = AICBT_UART_BAUD_DEFAULT,
    .uart_flowctrl = AICBT_UART_FC_DEFAULT,
    .lpm_enable    = AICBT_LPM_ENABLE_DEFAULT,
    .txpwr_lvl     = AICBT_TXPWR_LVL_DEFAULT,
};

int aicbt_patch_table_load(struct aic_usb_dev *usbdev, struct aicbt_patch_table *_head)
{
	struct aicbt_patch_table *head, *p;
	int ret = 0, i;
	uint32_t *data = NULL;

	head = _head;
	for (p = head; p != NULL; p = p->next) {
		data = p->data;
		if(AICBT_PT_BTMODE == p->type){
			*(data + 1)  = aicbsp_info.hwinfo < 0;
			*(data + 3) = aicbsp_info.hwinfo;
			*(data + 5)  = aicbsp_info.cpmode;

			*(data + 7) = aicbt_info.btmode;
			*(data + 9) = aicbt_info.btport;
			*(data + 11) = aicbt_info.uart_baud;
			*(data + 13) = aicbt_info.uart_flowctrl;
			*(data + 15) = aicbt_info.lpm_enable;
			*(data + 17) = aicbt_info.txpwr_lvl;

		}
		if (p->type == 0x06) {
			char *data_s = (char *)p->data;
			printk("patch version %s\n", data_s);
			continue;
		}
		for (i = 0; i < p->len; i++) {
			ret = rwnx_send_dbg_mem_write_req(usbdev, *data, *(data + 1));
			if (ret != 0)
				return ret;
			data += 2;
		}
		if (p->type == AICBT_PT_PWRON)
			udelay(500);
	}
	aicbt_patch_table_free(&head);
	return 0;
}


int rwnx_plat_bin_fw_patch_table_upload_android(struct aic_usb_dev *usbdev, char *filename){
    struct device *dev = usbdev->dev;
	struct aicbt_patch_table *head = NULL;
	struct aicbt_patch_table *new = NULL;
	struct aicbt_patch_table *cur = NULL;
   	 int size;
	int ret = 0;
   	uint8_t *rawdata=NULL;
	uint8_t *p = NULL;

    /* load aic firmware */
    size = aic_load_firmware((u32 **)&rawdata, filename, dev);

	/* Copy the file on the Embedded side */
    printk("### Upload %s fw_patch_table, size=%d\n", filename, size);

	if (size <= 0) {
		printk("wrong size of firmware file\n");
		ret = -1;
		goto err;
	}

	p = rawdata;

	if (memcmp(p, AICBT_PT_TAG, sizeof(AICBT_PT_TAG) < 16 ? sizeof(AICBT_PT_TAG) : 16)) {
		printk("TAG err\n");
		ret = -1;
		goto err;
	}
	p += 16;

	while (p - rawdata < size) {
		//printk("size = %d  p - rawdata = %d \r\n", size, p - rawdata);
		new = (struct aicbt_patch_table *)vmalloc(sizeof(struct aicbt_patch_table));
		memset(new, 0, sizeof(struct aicbt_patch_table));
		if (head == NULL) {
			head = new;
			cur  = new;
		} else {
			cur->next = new;
			cur = cur->next;
		}

		cur->name = (char *)vmalloc(sizeof(char) * 16);
		memset(cur->name, 0, sizeof(char) * 16);
		memcpy(cur->name, p, 16);
		p += 16;

		cur->type = *(uint32_t *)p;
		p += 4;

		cur->len = *(uint32_t *)p;
		p += 4;

		if((cur->type )  >= 1000 ) {//Temp Workaround
			cur->len = 0;
		}else{
			cur->data = (uint32_t *)vmalloc(sizeof(uint8_t) * cur->len * 8);
			memset(cur->data, 0, sizeof(uint8_t) * cur->len * 8);
			memcpy(cur->data, p, cur->len * 8);
			p += cur->len * 8;
		}
	}

	vfree(rawdata);
	aicbt_patch_table_load(usbdev, head);
	printk("fw_patch_table download complete\n\n");

	return ret;
err:
	//aicbt_patch_table_free(&head);

	if (rawdata){
		vfree(rawdata);
	}
	return ret;
}


