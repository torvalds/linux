/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2010 ROCKCHIP, Inc.
 * Author: roger_chen <cz@rock-chips.com>
 *
 * This program is the virtual flash device 
 * used to store bd_addr or MAC
 *
 */


#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/platform_device.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include "eth_mac.h"

#if 1
#define DBG(x...)   printk("eth_mac:" x)
#else
#define DBG(x...)
#endif

#define VERSION "0.1"

#define WLAN_MAC_FILE "/data/misc/wifi/wlan_mac"

extern char GetSNSectorInfo(char * pbuf);

int eth_mac_read_from_IDB(u8 *mac)
{
	int i;
	char *tempBuf = kmalloc(512, GFP_KERNEL);

	if(mac == NULL)
		return -EFAULT;

	GetSNSectorInfo(tempBuf);
#if 0
	for (i = 0; i < 512; i++) {
		printk("%02x, ", tempBuf[i]);
		if(((i+1)%16) == 0) printk("\n");
	}
#endif
	for (i = 506; i <= 511; i++) {
		mac[i-506] = tempBuf[i];
	}

	kfree(tempBuf);

	return 0;
}

int eth_mac_idb(u8 *eth_mac)
{
	int i;
	int err = 0;
	memset(eth_mac, 0, 6);
	err = eth_mac_read_from_IDB(eth_mac);
	if (err)
		return -1;
	printk("Read the Ethernet MAC address from IDB:");
	for (i = 0; i < 5; i++)
		printk("%2.2x:", eth_mac[i]);
	printk("%2.2x\n", eth_mac[i]);

	return 0;
}

#if 0
/**
*大写转小写
*
*/
static void   to_lower(char   *str) 
{ 
	int   i=0; 
	while(str[i]!=0) 
	{ 
		if((str[i] >= 'A')&&(str[i] <= 'Z')) 
			str[i]+=32; 
		i++; 
	} 
} 

/**
  *字符串格式转为mac 格式.
  *
  *
  */
static void  trans( char *src ,int * k) 
{
    char c;
    int i;
    int temp;
    int temp2;

    if( (src == NULL) ||(strlen(src) <16 ) ) // 参数检查
    {
      printk( "Arg Error\n" );
      return ;
    }

    for( i = 0; i < 6; i++ )
    {
      temp = 0;
      temp2 = 0;
      c = *src;
      if( c == ':' ){
	  	src++;
		c = *src;
      	}
      if( c >= 'a' && c <= 'f' )          // 两个字符中的第一个 比如 "0f" ,则表示是字符 '0'
         temp = ( c - 'a' ) + 10; 
      else
         temp = ( c - '0' ) ;
      src++;
      
      c = *src;
      if( i == 5){                         //wifi mac 末尾加1
	if(c =='f'){
	   c = '0';
	} if (c == '9'){
	   c = 'a';
	}else{
	   c = c + 1;
	}
      }
      if( c >= 'a' && c <= 'f' )             // 两个字符中的第二个,如 "f8" ,那么表示字符 '8'
         temp2 = ( c - 'a' ) + 10;
      else
         temp2 = ( c - '0' ) ;

      temp = temp * 16;
      temp += temp2;
      src++;
      *(k+i) = temp;

    }

}

int eth_mac_wifi(u8 *eth_mac){
	int i;
	struct  file *file = NULL;
	char wifi_mac[32];
	mm_segment_t old_fs;
	ssize_t ret;
	int maci[6];

	memset(eth_mac, 0, 6);
	
	file = filp_open(WLAN_MAC_FILE, O_RDWR,0);
	if (IS_ERR(file))
	{
		printk("open %s failed.", WLAN_MAC_FILE);
		return -ENOENT;
	} 

	old_fs = get_fs();
    	set_fs(get_ds());
    
    	file->f_op->llseek(file,0,0);
    	ret = file->f_op->read(file, wifi_mac, 32, &file->f_pos);
    
    	set_fs(old_fs);
    
   	 if(ret > 0){
        	//printk("mac read from %s: %s\n", WLAN_MAC_FILE,wifi_mac);

		to_lower(wifi_mac);
		trans(wifi_mac,maci);
		for (i = 0;i< 6;i++){
			eth_mac[i] = maci[i];
		}
		
   	 }
    	 else if(ret == 0)
       	printk("read nothing from %s........\n",WLAN_MAC_FILE);
    	 else 
        {
            	printk("read wifi mac error\n");
           	 return -ENOENT;
        }

	filp_close(file,NULL);
	return 0;
	
}

#endif
