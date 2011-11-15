/*
**********************************************************************************************************************
*                                                    Test 
*                                          software test for drivers
*                                              Test Sub-System
*
*                                   (c) Copyright 2007-2010, Grace.Miao China
*                                             All Rights Reserved
*
* Moudle  : test driver
* File    : nand_test.c
* Purpose : test driver of nand driver in Linux 
*
* By      : Grace Miao
* Version : v1.0
* Date    : 2011-3-16
* history : 
             2011-3-16     build the file     Grace Miao
**********************************************************************************************************************
*/
#include <linux/device.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/stat.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/ioport.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/scatterlist.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/cpufreq.h>
#include <linux/sched.h>
#include "../src/include/nand_type.h"
#include "../src/include/nand_drv_cfg.h"
#include "../src/include/nand_format.h"
#include "../src/include/nand_logic.h"
#include "../src/include/nand_oal.h"
#include "../src/include/nand_physic.h"
#include "../src/include/nand_scan.h"
#include "../src/include/nand_simple.h"

#include "../nfd/nand_blk.h"
#include "../nfd/mbr.h"

#include "nand_test.h"

#ifdef CONFIG_SUN4I_NANDFLASH_TEST     //  open nand test module

#define NAND_TEST  "[nand_test]:"

#define RESULT_OK   (0)
#define RESULT_FAIL   (1)

#define MAX_SECTORS       (100)         // max alloc buffer
#define BUFFER_SIZE       (512*MAX_SECTORS)

static ssize_t nand_test_store(struct kobject *kobject,struct attribute *attr, const char *buf, size_t count);
static ssize_t nand_test_show(struct kobject *kobject,struct attribute *attr, char *buf);

void obj_test_release(struct kobject *kobject);



struct nand_test_card {
    u8    *buffer;
    u8    *scratch;
    unsigned int sector_cnt;
};

struct nand_test_case {
    const char *name;
    int  sectors_cnt;
    int (*prepare)(struct nand_test_card *, int sectors_cnt);
    int (*run)(struct nand_test_card * );
    int (*cleanup)(struct nand_test_card *);
};

struct attribute prompt_attr = {
    .name = "nand_test",
    .mode = S_IRWXUGO
};

static struct attribute *def_attrs[] = {
    &prompt_attr,
    NULL
};


struct sysfs_ops obj_test_sysops =
{
    .show =  nand_test_show,
    .store = nand_test_store
};

struct kobj_type ktype = 
{
    .release = obj_test_release,
    .sysfs_ops=&obj_test_sysops,
    .default_attrs=def_attrs
};

void obj_test_release(struct kobject *kobject)
{
    printk(NAND_TEST "release .\n");
}



/* prepare buffer data for read and write*/
static int __nand_test_prepare(struct nand_test_card *test, int sector_cnt,int write)
{
    int i;

    test->sector_cnt = sector_cnt;    

    if (write){
        memset(test->buffer, 0xDF, 512 * (sector_cnt) +4);
    }
    else {
        for (i = 0; i < 512 * (sector_cnt) + 4; i++){
            test->buffer[i] = i%256;
         }
    }

    return 0;
}


static int nand_test_prepare_write(struct nand_test_card *test, int sector_cnt)
{
    return __nand_test_prepare(test, sector_cnt, 1);
}

static int nand_test_prepare_read(struct nand_test_card *test, int sector_cnt)
{
    return __nand_test_prepare(test, sector_cnt, 0);
}


static int nand_test_prepare_pwm(struct nand_test_card *test, int sector_cnt)
{
    test->sector_cnt = sector_cnt; 
    return 0;
}


/* read /write one sector with out verification*/
static int nand_test_simple_transfer(struct nand_test_card *test,
                                    unsigned dev_addr,unsigned start, 
                                    unsigned nsector, int write)
{
    int ret;
    if (write){
      
#ifndef NAND_CACHE_RW
        ret = LML_Write(start, nsector, test->buffer + dev_addr); 
#else
        //printk("Ws %lu %lu \n",start, nsector);
        ret = NAND_CacheWrite(start, nsector, test->buffer + dev_addr);
#endif     
        if(ret){
            return -EIO;
        }
        return 0;
        }
    else { 
      
#ifndef NAND_CACHE_RW
        LML_FlushPageCache();
        ret = LML_Read(start, nsector, test->buffer + dev_addr);
#else
        //printk("Rs %lu %lu \n",start, nsector);
        LML_FlushPageCache();
        ret = NAND_CacheRead(start, nsector, test->buffer + dev_addr);
#endif                                                  // read
        
        if (ret){
            return -EIO;  
        } 
        return 0;
    }
}


/* read /write one or more sectors with verification*/
static int nand_test_transfer(struct nand_test_card *test,
                              unsigned dev_addr,unsigned start, 
                              unsigned nsector, int write)
{
    int ret;
    int i;
    
    if (!write){
        ret = nand_test_simple_transfer(test, 0, start, nsector, 1);  // write to sectors for read
        if (ret){
            return ret;
        }
        memset(test->buffer, 0, nsector * 512 +4);    // clean mem for read
    }
  
    if ( ( ret = nand_test_simple_transfer(test, dev_addr, start, nsector, write ) ) ) {   // read or write
        return ret;
    }
    if(write){     
        memset(test->buffer, 0, nsector * 512 + 4);    // clean mem for read
        ret = nand_test_simple_transfer(test, 0 , start, nsector, 0);   // read 
        if (ret){
            return ret;
        }
        for(i  = 0; i < nsector * 512; i++){    // verify data
            if (test->buffer[i] != 0xDF){
                printk(KERN_INFO "[nand_test] Ttest->buffer[i] = %d, i = %d, dev_addr = %d, nsector = %d\n", test->buffer[i],  i, dev_addr,nsector);
                return RESULT_FAIL;
            }
        } 
    }
    else {   //read 
        for(i  = 0 + dev_addr; i < nsector * 512 + dev_addr ; i++){   // verify data
    
            if (test->buffer[i] != (i-dev_addr)%256){
                printk(KERN_INFO "[nand_test] Ttest->buffer[i] = %d, i = %d, dev_addr = %d, nsector = %d\n", test->buffer[i],  i, dev_addr,nsector);
                return RESULT_FAIL;
            }
       }
    }
    return RESULT_OK;
}


  
/* write one sector without verification*/
static int nand_test_single_write(struct nand_test_card *test)
{
    int ret;


    ret = nand_test_simple_transfer(test, 0, 0, test->sector_cnt,1);
    
    if(ret){
        return ret;
    }
    return nand_test_simple_transfer(test, 0, DiskSize/2, test->sector_cnt, 1 );
   
}

/* read one sector without verification*/
static int nand_test_single_read(struct nand_test_card *test)
{
    int ret;

    ret = nand_test_simple_transfer(test, 0, 0, test->sector_cnt,0);
    if(ret){
        return ret;
    }
    return nand_test_simple_transfer(test, 0, DiskSize/2, test->sector_cnt, 0);
}

/* write one  sector with verification */
static int nand_test_verify_write(struct nand_test_card *test)
{
    return nand_test_transfer(test,  0, 1, test->sector_cnt, 1);
}

/* read one  sector with verification */
static int nand_test_verify_read(struct nand_test_card *test)
{
    return nand_test_transfer(test,  0, 1, test->sector_cnt, 0);
}

/* write multi sector with start sector num 5*/
static int nand_test_multi_write(struct nand_test_card *test)
{
    return nand_test_transfer(test,  0, 5, test->sector_cnt, 1);
}


/* write multi sector with start sector num 29*/

static int nand_test_multi_read(struct nand_test_card *test)
{
    return nand_test_transfer(test,  0, 29, test->sector_cnt, 0);
}

/* write from buffer+1, buffer+2, and buffer+3, where buffer is  4  bytes algin */
static int nand_test_align_write(struct nand_test_card *test)
{
    int ret;
    int i;
  
    for (i = 1;i < 4;i++) {
        ret = nand_test_transfer(test,  i, 1, test->sector_cnt, 1);
    }
    return ret;
}


/* read to buffer+1, buffer+2, and buffer+3, where buffer is  4  bytes algin */

static int nand_test_align_read(struct nand_test_card *test)
{
    int ret;
    int i;
  
    for (i = 1;i < 4;i++) {
        ret = nand_test_transfer(test,  i, 1, test->sector_cnt, 0);
    }
    if (ret){
        return ret;
    }
  
    return 0;
}

/* write to incorrect sector num such as -1, DiskSize,  DiskSize +1 */
static int nand_test_negstart_write(struct nand_test_card *test)
{
    int ret;
    
    /* start + sectnum > 0, start < 0*/
    ret = nand_test_simple_transfer(test,  0, -5 , 11, 1);
    
    if (!ret){
        return RESULT_FAIL;
    }
    printk(NAND_TEST "start + sectnum > 0 pass\n");

    /* start + sectnum < 0 , start < 0 */
    ret = nand_test_simple_transfer(test,  0, -62, 5, 1);
    
    if (!ret){
        return RESULT_FAIL;
    }
    return RESULT_OK;

}


/* read from negative sector num   start + sectnum > 0, and start + setnum < 0 */
static int nand_test_negstart_read(struct nand_test_card *test)
{
    int ret;

    /* start + sectnum > 0, start < 0*/
    ret = nand_test_simple_transfer(test,  0, -1, 3, 0);
    
    if (!ret){
        return RESULT_FAIL;
    }
    printk(NAND_TEST "start + sectnum > 0 pass\n");

    /* start + sectnum < 0 , start < 0 */
    ret = nand_test_simple_transfer(test,  0, -90, 15, 0);
    
    if (!ret){
        return RESULT_FAIL;
    }
    return RESULT_OK;
  
}

static int nand_test_beyond(struct nand_test_card *test, int write)
{
    int ret;


    ret = nand_test_simple_transfer(test,  0, DiskSize -3 , 5, write);
    
    if (!ret){ 
        
        return 1;
    }
    printk(NAND_TEST "DiskSize -3 , 5 pass\n");
    ret = nand_test_simple_transfer(test,  0, DiskSize -1 , 2, write);
    
    if (!ret){ 
        
        return 1;
    }
    printk(NAND_TEST "DiskSize -1 , 2 pass\n");
    ret = nand_test_simple_transfer(test,  0, DiskSize , 3, write);
    
    if (!ret){ 
        
        return 1;
    }

    printk(NAND_TEST "DiskSize , 3 pass\n");
    
    ret = nand_test_simple_transfer(test,  0, DiskSize + 3 , 0, write);
    
    if (!ret){ 
        
        return 1;
    }

    printk(NAND_TEST "DiskSize + 3 , 0 pass\n");

    ret = nand_test_simple_transfer(test,  0, DiskSize - 3 , -2, write);
    
    if (!ret){ 
        
        return 1;
    }

    printk(NAND_TEST "DiskSize - 3 , -2 pass\n");
    
    return RESULT_OK;
}


static int nand_test_beyond_write(struct nand_test_card *test)
{   
    return (nand_test_beyond(test, 1));
}


/* read from incorrect sector num such as -1, DiskSize(max sector num + 1),  DiskSize +1 */
static int nand_test_beyond_read(struct nand_test_card *test)
{
    
    return (nand_test_beyond(test, 0));
  
}



/* write all sectors from sector num 0 to DiskSize - 1(max sector num )*/
static int nand_test_write_all_ascending(struct nand_test_card *test)
{
    int ret; 
    int i = 0;
    int j = 0;

    printk(KERN_INFO "DiskSize = %x\n", DiskSize);
   
  
    for (i = 0; i < DiskSize; i++) {   // write all sectors  
        ret = nand_test_simple_transfer(test, 0, i, test->sector_cnt,1);
        if(ret){ 
            printk(KERN_INFO "nand_test_write_all_ascending fail, sector num %d\n", i);
            return ret;
        }
    }
   
    /* start check */
    printk(KERN_INFO "[nand test]:start check\n");
   
    for (i = 0; i < DiskSize; i++){
        memset(test->buffer, 0, test->sector_cnt * 512);  // clear buffer
        
        ret = nand_test_simple_transfer(test, 0 , i, test->sector_cnt, 0);   // read 
        if(ret){
            return ret;
        }
        
        for(j  = 0; j < test->sector_cnt * 512; j++)  {  // verify
            if (test->buffer[j] != 0xDF){
                printk(KERN_INFO "nand_test_write_all_ascending, Ttest->buffer[j] = %d, i = %d\n", test->buffer[j],  i);
                return RESULT_FAIL;
            }
        }
   
    }
    return RESULT_OK;
}


/* read all sectors from sector num 0 to DiskSize - 1(max sector num )*/
static int nand_test_read_all_ascending(struct nand_test_card *test)
{
    int ret; 
    int i = 0;
    int j = 0;

    /*  before reading, write */
    for (i = 0; i < DiskSize; i++) {
      
        ret = nand_test_simple_transfer(test, 0, i, test->sector_cnt,1);  // write all sectors
        if(ret){
            printk(KERN_INFO "nand_test_read_all_ascending fail, sector num %d\n", i);
            return ret;
        }
    }
   
   /* finish write,  start to read and check */
    for (i = 0; i < DiskSize; i++)
    {
        if (i%100000 == 0){
            printk(KERN_INFO "[nand test]: sector num:%d\n", i);
        }
        
        memset(test->buffer, 0, test->sector_cnt * 512);  // clear buffer
        
        ret = nand_test_simple_transfer(test, 0 , i, test->sector_cnt, 0);   // read 
        if(ret){
            return ret;
        }
        for(j  = 0 ; j < test->sector_cnt * 512  ; j++){
            if (test->buffer[j] != (j)%256){
                printk(KERN_INFO "nand_test_read_all_ascending fial! Ttest->buffer[j] = %d, i = %d\n", test->buffer[i],  j);
                return RESULT_FAIL;
            }
      
       }
    } 
    return RESULT_OK;
}


/* write all sectors from sector num  DiskSize - 1(max sector num ) to  0  */
static int nand_test_write_all_descending(struct nand_test_card *test)
{
    int ret; 
    int i = 0;
    int j = 0;

    printk(KERN_INFO "nand_test: DiskSize = %x\n", DiskSize);
   
    for (i = DiskSize - 1; i >= 0; i--){
      
        memset(test->buffer, i%256, 512);
        
        if (i%100000 == 0){
            printk(KERN_INFO "[nand test]: sector num:%d\n", i);
        }
        
        ret = nand_test_simple_transfer(test, 0, i, test->sector_cnt,1);  // write all sectors
        if(ret){
            printk(KERN_INFO "[nand_test]: nand_test_write_all_ascending fail, sector num %d\n", i);
            return ret;
        }
   }
   
   printk(KERN_INFO "[nand test]: check start\n");
   
   for (i = DiskSize - 1; i >= 0; i--){

       if (i%100000 == 0){
           printk(KERN_INFO "[nand test]: sector num:%d\n", i);
       }
       
       memset(test->buffer, 0, test->sector_cnt * 512);  // clear buffer
       
       ret = nand_test_simple_transfer(test, 0 , i, test->sector_cnt, 0);   // read 
       if(ret){
           return ret;
       }
       for(j  = 0; j < 512; j++){  // verify
            if (test->buffer[j] != i%256){
                printk(KERN_INFO "[nand_test]: nand_test_write_all_ascending, Ttest->buffer[j] = %d, i = %d\n", test->buffer[j],  i);
                return RESULT_FAIL;
            }
        }   
    }
    return RESULT_OK;
}

/* read all sectors from sector num  DiskSize - 1(max sector num ) to  0  */
static int nand_test_read_all_descending(struct nand_test_card *test)
{
    int ret; 
    int i = 0;
    int j = 0;
   
    for (i = DiskSize - 1; i >= 0; i--){
        memset(test->buffer, i%256, 512);
        
        ret = nand_test_simple_transfer(test, 0, i, test->sector_cnt,1);  // write all sectors
        if(ret){
            printk(KERN_INFO "[nand_test]: nand_test_read_all_ascending fail, sector num %d\n", i);
            return ret;
        }
    }
   
    printk(KERN_INFO "[nand test]: check start\n");
    for (i = DiskSize - 1; i >= 0; i--){
        if (i%100000 == 0){
            printk(KERN_INFO "[nand test]: sector num:%d\n", i);
        }
        memset(test->buffer, 0, test->sector_cnt * 512);  // clear buffer
        ret = nand_test_simple_transfer(test, 0 , i, test->sector_cnt, 0);   // read 
        if(ret){
            return ret;
        }
        for(j = 0 ; j < test->sector_cnt * 512  ; j++){      // verify data
            if (test->buffer[j] != (i)%256){
                printk(KERN_INFO "[nand_test]:nand_test_read_all_ascending fial! Ttest->buffer[j] = %d, i = %d\n", test->buffer[j],  i);
                return RESULT_FAIL;
            }
        }
    } 
    return RESULT_OK;
}

/* write a sector for  n times  without verification to test stability */
static int nand_test_repeat_single_write(struct nand_test_card *test)
{
    int ret; 
    int i = 0;
   
    printk(NAND_TEST "DiskSize = %d\n", DiskSize);
    
    for(i = 0; i < REPEAT_TIMES*1000; i++){
       ret = nand_test_simple_transfer(test, 0 , DiskSize/7, test->sector_cnt, 1);
       if(ret){
           return ret;
       }
    }
    return 0;
}


/* read a sector for  n times with verification to test stability*/
static int nand_test_repeat_single_read(struct nand_test_card *test)
{
    int ret; 
    int i = 0;
    for(i = 0; i < REPEAT_TIMES*30; i++){
        ret = nand_test_simple_transfer(test, 0 , DiskSize/4 + 7, test->sector_cnt, 0);
        if(ret){
            return ret;
        }
   }
   return 0;
}

/* write multi sectors for  n times without verification to test stability*/
static int nand_test_repeat_multi_write(struct nand_test_card *test)
{
    int ret; 
    int i = 0;
    
    for(i = 0; i < 1100000; i++){
        ret = nand_test_simple_transfer(test, 0 , DiskSize/2, test->sector_cnt, 1);
        if(ret) {
            return ret;
        }
    }
    
    return 0;
}


/* read multi sectors for  n times without verification to test stability*/
static int nand_test_repeat_multi_read(struct nand_test_card *test)
{
    int ret; 
    int i = 0;
    for(i = 0; i < 9200000; i++){
        ret = nand_test_simple_transfer(test, 0 , DiskSize/3, test->sector_cnt, 0);
        if(ret){
            return ret;
       }
    }
    return 0;
}

/* random write one or more sectors*/
static int nand_test_random_write(struct nand_test_card *test)
{
    int ret; 
  
    ret = nand_test_simple_transfer(test, 0 , 0, test->sector_cnt, 1);
    if(ret){
        return ret;
    }
    
    ret = nand_test_simple_transfer(test, 0 , DiskSize -1, test->sector_cnt, 1);
    if(ret) {
        return ret;
    }
   
    ret = nand_test_simple_transfer(test, 0 , DiskSize/2, test->sector_cnt, 1);
    if(ret){
        return ret;
    }
    return 0;
}

/* random read one or more sectors*/
static int nand_test_random_read(struct nand_test_card *test)
{
    int ret; 
  
    ret = nand_test_simple_transfer(test, 0 , 0, test->sector_cnt, 0);
    if(ret) {
        return ret;
    }
   
    ret = nand_test_simple_transfer(test, 0 , DiskSize -1, test->sector_cnt, 0);
    if(ret){
        return ret;
    }
   
    ret = nand_test_simple_transfer(test, 0 , DiskSize/2, test->sector_cnt, 0);
    if(ret){
        return ret;
    }
   
    return 0;
}


/* clear r/w buffer to 0*/
static int nand_test_cleanup(struct nand_test_card *test)
{
    memset(test->buffer, 0, 512* (test->sector_cnt));
    return 0;
}


/* test cases */

static const struct nand_test_case nand_test_cases[] = {
    {
	    .name = "single sector write (no data verification)",
	    .sectors_cnt = 1,
	    .prepare = nand_test_prepare_write,
	    .run = nand_test_single_write,
	    .cleanup = nand_test_cleanup
    },
  
    {
	    .name = "single sector read (no data verification)",
	    .sectors_cnt = 1,
	    .prepare = nand_test_prepare_read,
	    .run = nand_test_single_read,
	    .cleanup = nand_test_cleanup
    },
  
    {
	    .name = "single sector write(verify data)",
	    .sectors_cnt = 1,
	    .prepare = nand_test_prepare_write,
	    .run = nand_test_verify_write,
	    .cleanup = nand_test_cleanup
    },
  
    {
	    .name = "single sector read(verify data)",
	    .sectors_cnt = 1,
	    .prepare = nand_test_prepare_read,
	    .run = nand_test_verify_read,
	    .cleanup = nand_test_cleanup
    },
  
    /* multi read/write*/
    {
	    .name = "multi sector read(2 sectors, verify)",
	    .sectors_cnt = 2,
	    .prepare = nand_test_prepare_read,
	    .run = nand_test_multi_read,
	    .cleanup = nand_test_cleanup
    },

    {
	    .name = "multi sector read(3 sectors, verify)",
	    .sectors_cnt = 3,
	    .prepare = nand_test_prepare_read,
	    .run = nand_test_multi_read,
	    .cleanup = nand_test_cleanup
    },

    {
	    .name = "multi sector read(8 sectors, verify)",
	    .sectors_cnt = 8,
	    .prepare = nand_test_prepare_read,
	    .run = nand_test_multi_read,
	    .cleanup = nand_test_cleanup
    },
  
    {
	    .name = "multi sector read(18 sectors, verify)",
	    .sectors_cnt = 18,
	    .prepare = nand_test_prepare_read,
	    .run = nand_test_multi_read,
	    .cleanup = nand_test_cleanup
    },
  
    {
	    .name = "multi sector read(53 sectors, verify)",
	    .sectors_cnt = 53,
	    .prepare = nand_test_prepare_read,
	    .run = nand_test_multi_read,
	    .cleanup = nand_test_cleanup
    },
  
    {
	    .name = "multi sector write(2 sectors ,verify)",
	    .sectors_cnt = 2,
	    .prepare = nand_test_prepare_write,
	    .run = nand_test_multi_write,
	    .cleanup = nand_test_cleanup
    },
  
    {
	    .name = "multi sector write(5 sectors ,verify)",
	    .sectors_cnt = 5,
	    .prepare = nand_test_prepare_write,
	    .run = nand_test_multi_write,
	    .cleanup = nand_test_cleanup,
    },
  
    {
	    .name = "multi sector write(12 sectors ,verify)",
	    .sectors_cnt = 12,
	    .prepare = nand_test_prepare_write,
	    .run = nand_test_multi_write,
	    .cleanup = nand_test_cleanup,
    },
  
    {
	    .name = "multi sector write(15 sectors ,verify)",
	    .sectors_cnt = 15,
	    .prepare = nand_test_prepare_write,
	    .run = nand_test_multi_write,
	    .cleanup = nand_test_cleanup,
    },

    {
	    .name = "multi sector write(26 sectors ,verify)",
	    .sectors_cnt = 26,
	    .prepare = nand_test_prepare_write,
	    .run = nand_test_multi_write,
	    .cleanup = nand_test_cleanup,
    },

    {
	    .name = "multi sector write(93 sectors ,verify)",
	    .sectors_cnt = 93,
	    .prepare = nand_test_prepare_write,
	    .run = nand_test_multi_write,
	    .cleanup = nand_test_cleanup,
    },
    
    /*align test*/
    {
	    .name = "align write(1 sector ,verify)",
	    .sectors_cnt = 1,
	    .prepare = nand_test_prepare_write,
	    .run = nand_test_align_write,
	    .cleanup = nand_test_cleanup,
    },
    
    {
	    .name = "align read(1 sector ,verify)",
	    .sectors_cnt = 1,
	    .prepare = nand_test_prepare_read,
	    .run = nand_test_align_read,
	    .cleanup = nand_test_cleanup,
    },
    
    /* stability test */
    {
	    .name = "weird write(negative start)",   // 18
	    .sectors_cnt = 10,
	    .prepare = nand_test_prepare_write,
	    .run = nand_test_negstart_write,
	    .cleanup = nand_test_cleanup,
    },
  
    {
	    .name = "weid read(nagative satrt)",
	    .sectors_cnt = 10,
	    .prepare = nand_test_prepare_read,
	    .run = nand_test_negstart_read,
	    .cleanup = nand_test_cleanup,
    },

    {
	    .name = "weird write(beyond start)",   // 20
	    .sectors_cnt = 10,
	    .prepare = nand_test_prepare_write,
	    .run = nand_test_beyond_write,
	    .cleanup = nand_test_cleanup,
    },
  
    {
	    .name = "weid read(bayond start)",
	    .sectors_cnt = 10,
	    .prepare = nand_test_prepare_read,
	    .run = nand_test_beyond_read,
	    .cleanup = nand_test_cleanup,
    },

    {                                            // 22
	    .name = "write all ascending",
	    .sectors_cnt = 1,
	    .prepare = nand_test_prepare_write,
	    .run = nand_test_write_all_ascending,
	    .cleanup = nand_test_cleanup,
    },

    {
	    .name = "read all ascending",
	    .sectors_cnt = 1,
	    .prepare = nand_test_prepare_read,
	    .run = nand_test_read_all_ascending,
	    .cleanup = nand_test_cleanup,
    },
  
    {
	    .name = "write all descending",
	    .sectors_cnt = 1,
	    .prepare = nand_test_prepare_write,
	    .run = nand_test_write_all_descending,
	     .cleanup = nand_test_cleanup,
    },
       

    {
	    .name = "read all descending",
	    .sectors_cnt = 1,
	    .prepare = nand_test_prepare_read,
	    .run = nand_test_read_all_descending,
	    .cleanup = nand_test_cleanup,
    },
   
    {                                                     // 26    
	    .name = " repeat  write (no data verification) ",
	    .sectors_cnt = 1,
	    .prepare = nand_test_prepare_write,
	    .run = nand_test_repeat_single_write,
	    .cleanup = nand_test_cleanup,
    },
  
    {                                                    // 27   
	    .name = " repeat  read (no data verification) ",
	    .sectors_cnt = 1,
	    .prepare = nand_test_prepare_read,
	    .run = nand_test_repeat_single_read,
	    .cleanup = nand_test_cleanup,
   },

   {                                                     // 28  
	    .name = " repeat multi write (no data verification)",
	    .sectors_cnt = 43,
	    .prepare = nand_test_prepare_write,
	    .run = nand_test_repeat_multi_write,
	    .cleanup = nand_test_cleanup,
   },
   
   {                                                    // 29    
	    .name = " repeat multi read (no data verification)",
	    .sectors_cnt = 81,
	    .prepare = nand_test_prepare_read,
	    .run = nand_test_repeat_multi_read,
	    .cleanup = nand_test_cleanup,
    },

    {                                                    // 30   
	    .name = " random  write (no data verification)",
	    .sectors_cnt = 1,
	    .prepare = nand_test_prepare_write,
	    .run = nand_test_random_write,
	    .cleanup = nand_test_cleanup,
    },
    
    {                                                    // 31   
	    .name = " random  read (no data verification)",
	    .sectors_cnt = 1,
	    .prepare = nand_test_prepare_read,
	    .run = nand_test_random_read,
	    .cleanup = nand_test_cleanup,
    },
    
    {                                                    // 32 
	    .name = " pwm  test (no data verification)",
	    .sectors_cnt = 1,
	    .prepare = nand_test_prepare_pwm,
	    //.run = nand_test_pwm,
	    .cleanup = nand_test_cleanup,
    },
};

static DEFINE_MUTEX(nand_test_lock);


/* run test cases*/
static void nand_test_run(struct nand_test_card *test, int testcase)
{
    int i, ret;
  
    printk(KERN_INFO "[nand_test]: Starting tests of nand\n");
  
    for (i = 0;i < ARRAY_SIZE(nand_test_cases);i++) {
        if (testcase && ((i + 1) != testcase)){
            continue;
        }
  
        printk(KERN_INFO "[nand_test]: Test case %d. %s...\n", i + 1, nand_test_cases[i].name);
    
        if (nand_test_cases[i].prepare) {
              ret = nand_test_cases[i].prepare(test, nand_test_cases[i].sectors_cnt);
          if (ret) {
              printk(KERN_INFO "[nand_test]: Result: Prepare stage failed! (%d)\n", ret);
              continue;
          }
        }
  
        ret = nand_test_cases[i].run(test);
        
        switch (ret) {
            case RESULT_OK:
                printk(KERN_INFO "[nand_test]: Result: OK\n");
                break;
            case RESULT_FAIL:
                printk(KERN_INFO "[nand_test]:Result: FAILED\n");
                break;
                //    case RESULT_UNSUP_HOST:                //grace del
                //      printk(KERN_INFO "%s: Result: UNSUPPORTED "
                //        "(by host)\n",
                //        mmc_hostname(test->card->host));
                //      break;
                //    case RESULT_UNSUP_CARD:
                //      printk(KERN_INFO "%s: Result: UNSUPPORTED "
                //        "(by card)\n",
                //        mmc_hostname(test->card->host));
                //      break;
            default:
                printk(KERN_INFO "[nand_test]:Result: ERROR (%d)\n", ret);
        }
    
        if (nand_test_cases[i].cleanup) {
            ret = nand_test_cases[i].cleanup(test);
            if (ret) {
                printk(KERN_INFO "[nand_test]:Warning: Cleanup"
                       "stage failed! (%d)\n", ret);
            }
        }
    }
  
    //mmc_release_host(test->card->host);
    
    printk(KERN_INFO "[nand_test]: Nand tests completed.\n");
}


/* do nothing */
static ssize_t nand_test_show(struct kobject *kobject,struct attribute *attr, char *buf)
{
    return 0;
}


/* receive testcase num from echo command */
static ssize_t nand_test_store(struct kobject *kobject,struct attribute *attr, const char *buf, size_t count)
{
  
    struct nand_test_card *test;
    int testcase;
  
    testcase = simple_strtol(buf, NULL, 10);  // get test case number     >> grace
  
    test = kzalloc(sizeof(struct nand_test_card), GFP_KERNEL);
    if (!test){
        return -ENOMEM;
    }
  
    test->buffer = kzalloc(BUFFER_SIZE, GFP_KERNEL);  // alloc buffer for r/w
    test->scratch = kzalloc(BUFFER_SIZE, GFP_KERNEL); // not used now
    
    if (test->buffer && test->scratch) {
        mutex_lock(&nand_test_lock);
        nand_test_run(test, testcase);             // run test cases
        mutex_unlock(&nand_test_lock);
    }
  
  
    kfree(test->buffer);
    kfree(test->scratch);
    kfree(test);
  
    return count;
}

struct kobject kobj;


/* if nand driver is not inited ,  functions below will be used  */
#ifdef INIT_NAND_IN_TESTDRIVER   

static void set_nand_pio(void)
{
    __u32 cfg0;
    __u32 cfg1;
    __u32 cfg2;
    void* gpio_base;
  
    //modify for f20
    gpio_base = (void *)SW_VA_PORTC_IO_BASE;
  
    cfg0 = *(volatile __u32 *)(gpio_base + 0x48);
    cfg1 = *(volatile __u32 *)(gpio_base + 0x4c);
    cfg2 = *(volatile __u32 *)(gpio_base + 0x50);
  
    /*set PIOC for nand*/
    cfg0 &= 0x0;
    cfg0 |= 0x22222222;
    cfg1 &= 0x0;
    cfg1 |= 0x22222222;
    cfg2 &= 0x0;
    cfg2 |= 0x22222222;
  
    *(volatile __u32 *)(gpio_base + 0x48) = cfg0;
    *(volatile __u32 *)(gpio_base + 0x4c) = cfg1;
    *(volatile __u32 *)(gpio_base + 0x50) = cfg2;

    //iounmap(gpio_base);
}

static __u32 get_cmu_clk(void)
{
	__u32 reg_val;
	__u32 div_p, factor_n;
	__u32 factor_k, factor_m;
	__u32 clock;

	reg_val  = *(volatile unsigned int *)(0xf1c20000 + 0x20);
	div_p    = (reg_val >> 16) & 0x3;
	factor_n = (reg_val >> 8) & 0x1f;
	factor_k = ((reg_val >> 4) & 0x3) + 1;
	factor_m = ((reg_val >> 0) & 0x3) + 1;

	clock = 24 * factor_n * factor_k/div_p/factor_m;
	printk("cmu_clk is %d \n", clock);

	return clock;
}

static void set_nand_clock(__u32 nand_max_clock)
{
	__u32 edo_clk, cmu_clk;
	__u32 cfg;
	__u32 nand_clk_divid_ratio;
	
	/*open ahb nand clk */
	cfg = *(volatile __u32 *)(0xf1c20000 + 0x60);
	cfg |= (0x1<<13);
	*(volatile __u32 *)(0xf1c20000 + 0x60) = cfg;

	/*set nand clock*/
	//edo_clk = (nand_max_clock > 20)?(nand_max_clock-10):nand_max_clock;
	edo_clk = nand_max_clock * 2;

    cmu_clk = get_cmu_clk( );
	nand_clk_divid_ratio = cmu_clk / edo_clk;
	if (cmu_clk % edo_clk)
			nand_clk_divid_ratio++;
	if (nand_clk_divid_ratio){
		if (nand_clk_divid_ratio > 16)
			nand_clk_divid_ratio = 15;
		else
			nand_clk_divid_ratio--;
	}
	/*set nand clock gate on*/
	cfg = *(volatile __u32 *)(0xf1c20000 + 0x80);

	/*gate on nand clock*/
	cfg |= (1U << 31);
	/*take cmu pll as nand src block*/
	cfg &= ~(0x3 << 24);
	cfg |=  (0x2 << 24);
	//set divn = 0
	cfg &= ~(0x03 << 12);

	/*set ratio*/
	cfg &= ~(0x0f << 0);
	cfg |= (nand_clk_divid_ratio & 0xf) << 0;

	*(volatile __u32 *)(0xf1c20000 + 0x80) = cfg;
	
	printk("nand clk init end \n");
	printk("offset 0xc:  0x%x \n", *(volatile __u32 *)(0xf1c20000 + 0x60));
	printk("offset 0x14:  0x%x \n", *(volatile __u32 *)(0xf1c20000 + 0x80));
}

#endif

static int __init nand_test_init(void)
{
  
   int ret;
#ifdef INIT_NAND_IN_TESTDRIVER
    __u32 cmu_clk;
#endif

    printk("[nand_test]:nand_test_init test init.\n");
    if((ret = kobject_init_and_add(&kobj,&ktype,NULL,"nand")) != 0 ) {
        return ret; 
    }


#ifdef INIT_NAND_IN_TESTDRIVER

    /* init nand resource */
    printk("[nand_test]:init nand resource \n");
    //set nand clk
    set_nand_clock(20);
   
   //set nand pio
    set_nand_pio();
   
    clear_NAND_ZI();
   
    printk("/*********************************************************/ \n");
    printk("[nand_test]: init nand block layer start \n");
    printk("/*********************************************************/ \n");
   
    ret = PHY_Init();
    if (ret) {
     PHY_Exit();     
     return -1;
    }
   
    ret = SCN_AnalyzeNandSystem();
    if (ret < 0){
        return ret;
    }
   
    ret = PHY_ChangeMode(1);
    if (ret < 0){
        return ret; 
    }   
    ret = FMT_Init();
    if (ret < 0){
        return ret;
    }
    ret = FMT_FormatNand();
    if (ret < 0){
        return ret;
    }
    FMT_Exit();
   
    /*init logic layer*/
    ret = LML_Init();
    if (ret < 0){
        return ret;
    }
#ifdef NAND_CACHE_RW
    NAND_CacheOpen();
#endif

#endif
  
   return 0;  // init success
  
}



static void __exit nand_test_exit(void)
{
    printk("[nand_test]:nand test exit.\n");
    kobject_del(&kobj);
    
#ifdef INIT_NAND_IN_TESTDRIVER
    LML_FlushPageCache();
    BMM_WriteBackAllMapTbl();
    LML_Exit();
    FMT_Exit();
    PHY_Exit(); 
#ifdef NAND_CACHE_RW
    NAND_CacheOpen();
#endif

#endif

    return;
}
  
  
module_init(nand_test_init);
module_exit(nand_test_exit);


MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Nand test driver");
MODULE_AUTHOR("Grace Miao");
#endif
