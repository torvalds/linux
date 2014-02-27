#include <linux/fs.h>
#include <asm/uaccess.h>

#define WIFI_NVRAM_FILE_NAME "/data/nvram/APCFG/APRDEB/RDAWIFI"

static int nvram_read(char *filename, char *buf, ssize_t len, int offset)
{	
    struct file *fd;
    //ssize_t ret;
    int retLen = -1;
    
    mm_segment_t old_fs = get_fs();
    set_fs(KERNEL_DS);
    
    fd = filp_open(filename, O_WRONLY|O_CREAT, 0644);
    
    if(IS_ERR(fd)) {
        printk("[rda5890][nvram_read] : failed to open!!\n");
        return -1;
    }
    do{
        if ((fd->f_op == NULL) || (fd->f_op->read == NULL))
    		{
                printk("[rda5890][nvram_read] : file can not be read!!\n");
                break;
    		} 
    		
        if (fd->f_pos != offset) {
            if (fd->f_op->llseek) {
        		    if(fd->f_op->llseek(fd, offset, 0) != offset) {
						printk("[rda5890][nvram_read] : failed to seek!!\n");
					    break;
        		    }
        	  } else {
        		    fd->f_pos = offset;
        	  }
        }    		
        
    		retLen = fd->f_op->read(fd,
    									  buf,
    									  len,
    									  &fd->f_pos);			
    		
    }while(false);
    
    filp_close(fd, NULL);
    
    set_fs(old_fs);
    
    return retLen;
}

static int nvram_write(char *filename, char *buf, ssize_t len, int offset)
{	
    struct file *fd;
    int retLen = -1;
        
    mm_segment_t old_fs = get_fs();
    set_fs(KERNEL_DS);
    
    fd = filp_open(filename, O_WRONLY|O_CREAT, 0644);
    
    if(IS_ERR(fd)) {
        printk("[rda5890][nvram_write] : failed to open!!\n");
        return -1;
    }
    do{
        if ((fd->f_op == NULL) || (fd->f_op->write == NULL))
    		{
                printk("[rda5890][nvram_write] : file can not be write!!\n");
                break;
    		} /* End of if */
    		
        if (fd->f_pos != offset) {
            if (fd->f_op->llseek) {
        	    if(fd->f_op->llseek(fd, offset, 0) != offset) {
				    printk("[rda5890][nvram_write] : failed to seek!!\n");
                    break;
                }
            } else {
                fd->f_pos = offset;
            }
        }       		
        
        retLen = fd->f_op->write(fd,
                                 buf,
                                 len,
                                 &fd->f_pos);			
    		
    }while(false);
    
    filp_close(fd, NULL);
    
    set_fs(old_fs);
    
    return retLen;
}

int rda5890_read_mac(char* buf)
{
    return nvram_read(WIFI_NVRAM_FILE_NAME, buf, 6, 0);
}

int rda5890_write_mac(char * buf)
{
    return nvram_write(WIFI_NVRAM_FILE_NAME, buf, 6, 0);
}

