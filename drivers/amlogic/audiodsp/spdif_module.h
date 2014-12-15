#ifndef __SPDIF_MODULE_H__
#define __SPDIF_MODULE_H__
typedef struct {
	unsigned iec958_hw_start;
	unsigned iec958_hw_rd_offset;
	unsigned iec958_wr_offset;
	unsigned iec958_buffer_size;
}_iec958_data_info;


#define AUDIO_SPDIF_GET_958_BUF_SIZE      		_IOR('s', 0x01, unsigned long)
#define AUDIO_SPDIF_GET_958_BUF_CONTENT   	_IOR('s', 0x02, unsigned long)
#define AUDIO_SPDIF_GET_958_BUF_SPACE     		_IOR('s', 0x03, unsigned long)
#define AUDIO_SPDIF_GET_958_BUF_RD_OFFSET	_IOR('s', 0x04, unsigned long)	
#define AUDIO_SPDIF_GET_958_ENABLE_STATUS	 _IOR('s', 0x05, unsigned long)	
#define AUDIO_SPDIF_GET_I2S_ENABLE_STATUS	 _IOR('s', 0x06, unsigned long)	
#define AUDIO_SPDIF_SET_958_ENABLE	   		 _IOW('s', 0x07, unsigned long)	
#define AUDIO_SPDIF_SET_958_INIT_PREPARE	        _IOW('s', 0x08, unsigned long)	
#define AUDIO_SPDIF_SET_958_WR_OFFSET	   	  _IOW('s', 0x09, unsigned long)	

static int audio_spdif_open(struct inode *inode, struct file *file);
static int audio_spdif_release(struct inode *inode, struct file *file);
static long audio_spdif_ioctl( struct file *file, unsigned int cmd, unsigned long args);
static ssize_t audio_spdif_write(struct file *file, const char __user *userbuf,size_t len, loff_t * off);
static ssize_t audio_spdif_read(struct file *filp,	char __user *buffer,	size_t length,	 loff_t * offset);
static int audio_spdif_mmap(struct file *file, struct vm_area_struct *vma);

#endif
