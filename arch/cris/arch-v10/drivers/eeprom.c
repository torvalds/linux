/*!*****************************************************************************
*!
*!  Implements an interface for i2c compatible eeproms to run under Linux.
*!  Supports 2k, 8k(?) and 16k. Uses adaptive timing adjustments by
*!  Johan.Adolfsson@axis.com
*!
*!  Probing results:
*!    8k or not is detected (the assumes 2k or 16k)
*!    2k or 16k detected using test reads and writes.
*!
*!------------------------------------------------------------------------
*!  HISTORY
*!
*!  DATE          NAME              CHANGES
*!  ----          ----              -------
*!  Aug  28 1999  Edgar Iglesias    Initial Version
*!  Aug  31 1999  Edgar Iglesias    Allow simultaneous users.
*!  Sep  03 1999  Edgar Iglesias    Updated probe.
*!  Sep  03 1999  Edgar Iglesias    Added bail-out stuff if we get interrupted
*!                                  in the spin-lock.
*!
*!        (c) 1999 Axis Communications AB, Lund, Sweden
*!*****************************************************************************/

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/uaccess.h>
#include "i2c.h"

#define D(x)

/* If we should use adaptive timing or not: */
/* #define EEPROM_ADAPTIVE_TIMING */

#define EEPROM_MAJOR_NR 122  /* use a LOCAL/EXPERIMENTAL major for now */
#define EEPROM_MINOR_NR 0

/* Empirical sane initial value of the delay, the value will be adapted to
 * what the chip needs when using EEPROM_ADAPTIVE_TIMING.
 */
#define INITIAL_WRITEDELAY_US 4000
#define MAX_WRITEDELAY_US 10000 /* 10 ms according to spec for 2KB EEPROM */

/* This one defines how many times to try when eeprom fails. */
#define EEPROM_RETRIES 10

#define EEPROM_2KB (2 * 1024)
/*#define EEPROM_4KB (4 * 1024)*/ /* Exists but not used in Axis products */
#define EEPROM_8KB (8 * 1024 - 1 ) /* Last byte has write protection bit */
#define EEPROM_16KB (16 * 1024)

#define i2c_delay(x) udelay(x)

/*
 *  This structure describes the attached eeprom chip.
 *  The values are probed for.
 */

struct eeprom_type
{
  unsigned long size;
  unsigned long sequential_write_pagesize;
  unsigned char select_cmd;
  unsigned long usec_delay_writecycles; /* Min time between write cycles
					   (up to 10ms for some models) */
  unsigned long usec_delay_step; /* For adaptive algorithm */
  int adapt_state; /* 1 = To high , 0 = Even, -1 = To low */
  
  /* this one is to keep the read/write operations atomic */
  struct mutex lock;
  int retry_cnt_addr; /* Used to keep track of number of retries for
                         adaptive timing adjustments */
  int retry_cnt_read;
};

static int  eeprom_open(struct inode * inode, struct file * file);
static loff_t  eeprom_lseek(struct file * file, loff_t offset, int orig);
static ssize_t  eeprom_read(struct file * file, char * buf, size_t count,
                            loff_t *off);
static ssize_t  eeprom_write(struct file * file, const char * buf, size_t count,
                             loff_t *off);
static int eeprom_close(struct inode * inode, struct file * file);

static int  eeprom_address(unsigned long addr);
static int  read_from_eeprom(char * buf, int count);
static int eeprom_write_buf(loff_t addr, const char * buf, int count);
static int eeprom_read_buf(loff_t addr, char * buf, int count);

static void eeprom_disable_write_protect(void);


static const char eeprom_name[] = "eeprom";

/* chip description */
static struct eeprom_type eeprom;

/* This is the exported file-operations structure for this device. */
const struct file_operations eeprom_fops =
{
  .llseek  = eeprom_lseek,
  .read    = eeprom_read,
  .write   = eeprom_write,
  .open    = eeprom_open,
  .release = eeprom_close
};

/* eeprom init call. Probes for different eeprom models. */

int __init eeprom_init(void)
{
  mutex_init(&eeprom.lock);

#ifdef CONFIG_ETRAX_I2C_EEPROM_PROBE
#define EETEXT "Found"
#else
#define EETEXT "Assuming"
#endif
  if (register_chrdev(EEPROM_MAJOR_NR, eeprom_name, &eeprom_fops))
  {
    printk(KERN_INFO "%s: unable to get major %d for eeprom device\n",
           eeprom_name, EEPROM_MAJOR_NR);
    return -1;
  }
  
  printk("EEPROM char device v0.3, (c) 2000 Axis Communications AB\n");

  /*
   *  Note: Most of this probing method was taken from the printserver (5470e)
   *        codebase. It did not contain a way of finding the 16kB chips
   *        (M24128 or variants). The method used here might not work
   *        for all models. If you encounter problems the easiest way
   *        is probably to define your model within #ifdef's, and hard-
   *        code it.
   */

  eeprom.size = 0;
  eeprom.usec_delay_writecycles = INITIAL_WRITEDELAY_US;
  eeprom.usec_delay_step = 128;
  eeprom.adapt_state = 0;
  
#ifdef CONFIG_ETRAX_I2C_EEPROM_PROBE
  i2c_start();
  i2c_outbyte(0x80);
  if(!i2c_getack())
  {
    /* It's not 8k.. */
    int success = 0;
    unsigned char buf_2k_start[16];
    
    /* Im not sure this will work... :) */
    /* assume 2kB, if failure go for 16kB */
    /* Test with 16kB settings.. */
    /* If it's a 2kB EEPROM and we address it outside it's range
     * it will mirror the address space:
     * 1. We read two locations (that are mirrored), 
     *    if the content differs * it's a 16kB EEPROM.
     * 2. if it doesn't differ - write different value to one of the locations,
     *    check the other - if content still is the same it's a 2k EEPROM,
     *    restore original data.
     */
#define LOC1 8
#define LOC2 (0x1fb) /*1fb, 3ed, 5df, 7d1 */

   /* 2k settings */  
    i2c_stop();
    eeprom.size = EEPROM_2KB;
    eeprom.select_cmd = 0xA0;   
    eeprom.sequential_write_pagesize = 16;
    if( eeprom_read_buf( 0, buf_2k_start, 16 ) == 16 )
    {
      D(printk("2k start: '%16.16s'\n", buf_2k_start));
    }
    else
    {
      printk(KERN_INFO "%s: Failed to read in 2k mode!\n", eeprom_name);  
    }
    
    /* 16k settings */
    eeprom.size = EEPROM_16KB;
    eeprom.select_cmd = 0xA0;   
    eeprom.sequential_write_pagesize = 64;

    {
      unsigned char loc1[4], loc2[4], tmp[4];
      if( eeprom_read_buf(LOC2, loc2, 4) == 4)
      {
        if( eeprom_read_buf(LOC1, loc1, 4) == 4)
        {
          D(printk("0 loc1: (%i) '%4.4s' loc2 (%i) '%4.4s'\n", 
                   LOC1, loc1, LOC2, loc2));
#if 0
          if (memcmp(loc1, loc2, 4) != 0 )
          {
            /* It's 16k */
            printk(KERN_INFO "%s: 16k detected in step 1\n", eeprom_name);
            eeprom.size = EEPROM_16KB;     
            success = 1;
          }
          else
#endif
          {
            /* Do step 2 check */
            /* Invert value */
            loc1[0] = ~loc1[0];
            if (eeprom_write_buf(LOC1, loc1, 1) == 1)
            {
              /* If 2k EEPROM this write will actually write 10 bytes
               * from pos 0
               */
              D(printk("1 loc1: (%i) '%4.4s' loc2 (%i) '%4.4s'\n", 
                       LOC1, loc1, LOC2, loc2));
              if( eeprom_read_buf(LOC1, tmp, 4) == 4)
              {
                D(printk("2 loc1: (%i) '%4.4s' tmp '%4.4s'\n", 
                         LOC1, loc1, tmp));
                if (memcmp(loc1, tmp, 4) != 0 )
                {
                  printk(KERN_INFO "%s: read and write differs! Not 16kB\n",
                         eeprom_name);
                  loc1[0] = ~loc1[0];
                  
                  if (eeprom_write_buf(LOC1, loc1, 1) == 1)
                  {
                    success = 1;
                  }
                  else
                  {
                    printk(KERN_INFO "%s: Restore 2k failed during probe,"
                           " EEPROM might be corrupt!\n", eeprom_name);
                    
                  }
                  i2c_stop();
                  /* Go to 2k mode and write original data */
                  eeprom.size = EEPROM_2KB;
                  eeprom.select_cmd = 0xA0;   
                  eeprom.sequential_write_pagesize = 16;
                  if( eeprom_write_buf(0, buf_2k_start, 16) == 16)
                  {
                  }
                  else
                  {
                    printk(KERN_INFO "%s: Failed to write back 2k start!\n",
                           eeprom_name);
                  }
                  
                  eeprom.size = EEPROM_2KB;
                }
              }
                
              if(!success)
              {
                if( eeprom_read_buf(LOC2, loc2, 1) == 1)
                {
                  D(printk("0 loc1: (%i) '%4.4s' loc2 (%i) '%4.4s'\n", 
                           LOC1, loc1, LOC2, loc2));
                  if (memcmp(loc1, loc2, 4) == 0 )
                  {
                    /* Data the same, must be mirrored -> 2k */
                    /* Restore data */
                    printk(KERN_INFO "%s: 2k detected in step 2\n", eeprom_name);
                    loc1[0] = ~loc1[0];
                    if (eeprom_write_buf(LOC1, loc1, 1) == 1)
                    {
                      success = 1;
                    }
                    else
                    {
                      printk(KERN_INFO "%s: Restore 2k failed during probe,"
                             " EEPROM might be corrupt!\n", eeprom_name);
                      
                    }
                    
                    eeprom.size = EEPROM_2KB;     
                  }
                  else
                  {
                    printk(KERN_INFO "%s: 16k detected in step 2\n",
                           eeprom_name);
                    loc1[0] = ~loc1[0];
                    /* Data differs, assume 16k */
                    /* Restore data */
                    if (eeprom_write_buf(LOC1, loc1, 1) == 1)
                    {
                      success = 1;
                    }
                    else
                    {
                      printk(KERN_INFO "%s: Restore 16k failed during probe,"
                             " EEPROM might be corrupt!\n", eeprom_name);
                    }
                    
                    eeprom.size = EEPROM_16KB;
                  }
                }
              }
            }
          } /* read LOC1 */
        } /* address LOC1 */
        if (!success)
        {
          printk(KERN_INFO "%s: Probing failed!, using 2KB!\n", eeprom_name);
          eeprom.size = EEPROM_2KB;               
        }
      } /* read */
    }
  }
  else
  {
    i2c_outbyte(0x00);
    if(!i2c_getack())
    {
      /* No 8k */
      eeprom.size = EEPROM_2KB;
    }
    else
    {
      i2c_start();
      i2c_outbyte(0x81);
      if (!i2c_getack())
      {
        eeprom.size = EEPROM_2KB;
      }
      else
      {
        /* It's a 8kB */
        i2c_inbyte();
        eeprom.size = EEPROM_8KB;
      }
    }
  }
  i2c_stop();
#elif defined(CONFIG_ETRAX_I2C_EEPROM_16KB)
  eeprom.size = EEPROM_16KB;
#elif defined(CONFIG_ETRAX_I2C_EEPROM_8KB)
  eeprom.size = EEPROM_8KB;
#elif defined(CONFIG_ETRAX_I2C_EEPROM_2KB)
  eeprom.size = EEPROM_2KB;
#endif

  switch(eeprom.size)
  {
   case (EEPROM_2KB):
     printk("%s: " EETEXT " i2c compatible 2kB eeprom.\n", eeprom_name);
     eeprom.sequential_write_pagesize = 16;
     eeprom.select_cmd = 0xA0;
     break;
   case (EEPROM_8KB):
     printk("%s: " EETEXT " i2c compatible 8kB eeprom.\n", eeprom_name);
     eeprom.sequential_write_pagesize = 16;
     eeprom.select_cmd = 0x80;
     break;
   case (EEPROM_16KB):
     printk("%s: " EETEXT " i2c compatible 16kB eeprom.\n", eeprom_name);
     eeprom.sequential_write_pagesize = 64;
     eeprom.select_cmd = 0xA0;     
     break;
   default:
     eeprom.size = 0;
     printk("%s: Did not find a supported eeprom\n", eeprom_name);
     break;
  }

  

  eeprom_disable_write_protect();

  return 0;
}

/* Opens the device. */
static int eeprom_open(struct inode * inode, struct file * file)
{
  if(iminor(inode) != EEPROM_MINOR_NR)
     return -ENXIO;
  if(imajor(inode) != EEPROM_MAJOR_NR)
     return -ENXIO;

  if( eeprom.size > 0 )
  {
    /* OK */
    return 0;
  }

  /* No EEprom found */
  return -EFAULT;
}

/* Changes the current file position. */

static loff_t eeprom_lseek(struct file * file, loff_t offset, int orig)
{
/*
 *  orig 0: position from beginning of eeprom
 *  orig 1: relative from current position
 *  orig 2: position from last eeprom address
 */
  
  switch (orig)
  {
   case 0:
     file->f_pos = offset;
     break;
   case 1:
     file->f_pos += offset;
     break;
   case 2:
     file->f_pos = eeprom.size - offset;
     break;
   default:
     return -EINVAL;
  }

  /* truncate position */
  if (file->f_pos < 0)
  {
    file->f_pos = 0;    
    return(-EOVERFLOW);
  }
  
  if (file->f_pos >= eeprom.size)
  {
    file->f_pos = eeprom.size - 1;
    return(-EOVERFLOW);
  }

  return ( file->f_pos );
}

/* Reads data from eeprom. */

static int eeprom_read_buf(loff_t addr, char * buf, int count)
{
  return eeprom_read(NULL, buf, count, &addr);
}



/* Reads data from eeprom. */

static ssize_t eeprom_read(struct file * file, char * buf, size_t count, loff_t *off)
{
  int read=0;
  unsigned long p = *off;

  unsigned char page;

  if(p >= eeprom.size)  /* Address i 0 - (size-1) */
  {
    return -EFAULT;
  }
  
  if (mutex_lock_interruptible(&eeprom.lock))
    return -EINTR;

  page = (unsigned char) (p >> 8);
  
  if(!eeprom_address(p))
  {
    printk(KERN_INFO "%s: Read failed to address the eeprom: "
           "0x%08X (%i) page: %i\n", eeprom_name, (int)p, (int)p, page);
    i2c_stop();
    
    /* don't forget to wake them up */
    mutex_unlock(&eeprom.lock);
    return -EFAULT;
  }

  if( (p + count) > eeprom.size)
  {
    /* truncate count */
    count = eeprom.size - p;
  }

  /* stop dummy write op and initiate the read op */
  i2c_start();

  /* special case for small eeproms */
  if(eeprom.size < EEPROM_16KB)
  {
    i2c_outbyte( eeprom.select_cmd | 1 | (page << 1) );
  }

  /* go on with the actual read */
  read = read_from_eeprom( buf, count);
  
  if(read > 0)
  {
    *off += read;
  }

  mutex_unlock(&eeprom.lock);
  return read;
}

/* Writes data to eeprom. */

static int eeprom_write_buf(loff_t addr, const char * buf, int count)
{
  return eeprom_write(NULL, buf, count, &addr);
}


/* Writes data to eeprom. */

static ssize_t eeprom_write(struct file * file, const char * buf, size_t count,
                            loff_t *off)
{
  int i, written, restart=1;
  unsigned long p;

  if (!access_ok(VERIFY_READ, buf, count))
  {
    return -EFAULT;
  }

  /* bail out if we get interrupted */
  if (mutex_lock_interruptible(&eeprom.lock))
    return -EINTR;
  for(i = 0; (i < EEPROM_RETRIES) && (restart > 0); i++)
  {
    restart = 0;
    written = 0;
    p = *off;
   
    
    while( (written < count) && (p < eeprom.size))
    {
      /* address the eeprom */
      if(!eeprom_address(p))
      {
        printk(KERN_INFO "%s: Write failed to address the eeprom: "
               "0x%08X (%i) \n", eeprom_name, (int)p, (int)p);
        i2c_stop();
        
        /* don't forget to wake them up */
        mutex_unlock(&eeprom.lock);
        return -EFAULT;
      }
#ifdef EEPROM_ADAPTIVE_TIMING      
      /* Adaptive algorithm to adjust timing */
      if (eeprom.retry_cnt_addr > 0)
      {
        /* To Low now */
        D(printk(">D=%i d=%i\n",
               eeprom.usec_delay_writecycles, eeprom.usec_delay_step));

        if (eeprom.usec_delay_step < 4)
        {
          eeprom.usec_delay_step++;
          eeprom.usec_delay_writecycles += eeprom.usec_delay_step;
        }
        else
        {

          if (eeprom.adapt_state > 0)
          {
            /* To Low before */
            eeprom.usec_delay_step *= 2;
            if (eeprom.usec_delay_step > 2)
            {
              eeprom.usec_delay_step--;
            }
            eeprom.usec_delay_writecycles += eeprom.usec_delay_step;
          }
          else if (eeprom.adapt_state < 0)
          {
            /* To High before (toggle dir) */
            eeprom.usec_delay_writecycles += eeprom.usec_delay_step;
            if (eeprom.usec_delay_step > 1)
            {
              eeprom.usec_delay_step /= 2;
              eeprom.usec_delay_step--;
            }
          }
        }

        eeprom.adapt_state = 1;
      }
      else
      {
        /* To High (or good) now */
        D(printk("<D=%i d=%i\n",
               eeprom.usec_delay_writecycles, eeprom.usec_delay_step));
        
        if (eeprom.adapt_state < 0)
        {
          /* To High before */
          if (eeprom.usec_delay_step > 1)
          {
            eeprom.usec_delay_step *= 2;
            eeprom.usec_delay_step--;
            
            if (eeprom.usec_delay_writecycles > eeprom.usec_delay_step)
            {
              eeprom.usec_delay_writecycles -= eeprom.usec_delay_step;
            }
          }
        }
        else if (eeprom.adapt_state > 0)
        {
          /* To Low before (toggle dir) */
          if (eeprom.usec_delay_writecycles > eeprom.usec_delay_step)
          {
            eeprom.usec_delay_writecycles -= eeprom.usec_delay_step;
          }
          if (eeprom.usec_delay_step > 1)
          {
            eeprom.usec_delay_step /= 2;
            eeprom.usec_delay_step--;
          }
          
          eeprom.adapt_state = -1;
        }

        if (eeprom.adapt_state > -100)
        {
          eeprom.adapt_state--;
        }
        else
        {
          /* Restart adaption */
          D(printk("#Restart\n"));
          eeprom.usec_delay_step++;
        }
      }
#endif /* EEPROM_ADAPTIVE_TIMING */
      /* write until we hit a page boundary or count */
      do
      {
        i2c_outbyte(buf[written]);        
        if(!i2c_getack())
        {
          restart=1;
          printk(KERN_INFO "%s: write error, retrying. %d\n", eeprom_name, i);
          i2c_stop();
          break;
        }
        written++;
        p++;        
      } while( written < count && ( p % eeprom.sequential_write_pagesize ));

      /* end write cycle */
      i2c_stop();
      i2c_delay(eeprom.usec_delay_writecycles);
    } /* while */
  } /* for  */

  mutex_unlock(&eeprom.lock);
  if (written == 0 && p >= eeprom.size){
    return -ENOSPC;
  }
  *off = p;
  return written;
}

/* Closes the device. */

static int eeprom_close(struct inode * inode, struct file * file)
{
  /* do nothing for now */
  return 0;
}

/* Sets the current address of the eeprom. */

static int eeprom_address(unsigned long addr)
{
  int i;
  unsigned char page, offset;

  page   = (unsigned char) (addr >> 8);
  offset = (unsigned char)  addr;

  for(i = 0; i < EEPROM_RETRIES; i++)
  {
    /* start a dummy write for addressing */
    i2c_start();

    if(eeprom.size == EEPROM_16KB)
    {
      i2c_outbyte( eeprom.select_cmd ); 
      i2c_getack();
      i2c_outbyte(page); 
    }
    else
    {
      i2c_outbyte( eeprom.select_cmd | (page << 1) ); 
    }
    if(!i2c_getack())
    {
      /* retry */
      i2c_stop();
      /* Must have a delay here.. 500 works, >50, 100->works 5th time*/
      i2c_delay(MAX_WRITEDELAY_US / EEPROM_RETRIES * i);
      /* The chip needs up to 10 ms from write stop to next start */
     
    }
    else
    {
      i2c_outbyte(offset);
      
      if(!i2c_getack())
      {
        /* retry */
        i2c_stop();
      }
      else
        break;
    }
  }    

  
  eeprom.retry_cnt_addr = i;
  D(printk("%i\n", eeprom.retry_cnt_addr));
  if(eeprom.retry_cnt_addr == EEPROM_RETRIES)
  {
    /* failed */
    return 0;
  }
  return 1;
}

/* Reads from current address. */

static int read_from_eeprom(char * buf, int count)
{
  int i, read=0;

  for(i = 0; i < EEPROM_RETRIES; i++)
  {    
    if(eeprom.size == EEPROM_16KB)
    {
      i2c_outbyte( eeprom.select_cmd | 1 );
    }

    if(i2c_getack())
    {
      break;
    }
  }
  
  if(i == EEPROM_RETRIES)
  {
    printk(KERN_INFO "%s: failed to read from eeprom\n", eeprom_name);
    i2c_stop();
    
    return -EFAULT;
  }

  while( (read < count))
  {    
    if (put_user(i2c_inbyte(), &buf[read++]))
    {
      i2c_stop();

      return -EFAULT;
    }

    /*
     *  make sure we don't ack last byte or you will get very strange
     *  results!
     */
    if(read < count)
    {
      i2c_sendack();
    }
  }

  /* stop the operation */
  i2c_stop();

  return read;
}

/* Disables write protection if applicable. */

#define DBP_SAVE(x)
#define ax_printf printk
static void eeprom_disable_write_protect(void)
{
  /* Disable write protect */
  if (eeprom.size == EEPROM_8KB)
  {
    /* Step 1 Set WEL = 1 (write 00000010 to address 1FFFh */
    i2c_start();
    i2c_outbyte(0xbe);
    if(!i2c_getack())
    {
      DBP_SAVE(ax_printf("Get ack returns false\n"));
    }
    i2c_outbyte(0xFF);
    if(!i2c_getack())
    {
      DBP_SAVE(ax_printf("Get ack returns false 2\n"));
    }
    i2c_outbyte(0x02);
    if(!i2c_getack())
    {
      DBP_SAVE(ax_printf("Get ack returns false 3\n"));
    }
    i2c_stop();

    i2c_delay(1000);

    /* Step 2 Set RWEL = 1 (write 00000110 to address 1FFFh */
    i2c_start();
    i2c_outbyte(0xbe);
    if(!i2c_getack())
    {
      DBP_SAVE(ax_printf("Get ack returns false 55\n"));
    }
    i2c_outbyte(0xFF);
    if(!i2c_getack())
    {
      DBP_SAVE(ax_printf("Get ack returns false 52\n"));
    }
    i2c_outbyte(0x06);
    if(!i2c_getack())
    {
      DBP_SAVE(ax_printf("Get ack returns false 53\n"));
    }
    i2c_stop();
    
    /* Step 3 Set BP1, BP0, and/or WPEN bits (write 00000110 to address 1FFFh */
    i2c_start();
    i2c_outbyte(0xbe);
    if(!i2c_getack())
    {
      DBP_SAVE(ax_printf("Get ack returns false 56\n"));
    }
    i2c_outbyte(0xFF);
    if(!i2c_getack())
    {
      DBP_SAVE(ax_printf("Get ack returns false 57\n"));
    }
    i2c_outbyte(0x06);
    if(!i2c_getack())
    {
      DBP_SAVE(ax_printf("Get ack returns false 58\n"));
    }
    i2c_stop();
    
    /* Write protect disabled */
  }
}
device_initcall(eeprom_init);
