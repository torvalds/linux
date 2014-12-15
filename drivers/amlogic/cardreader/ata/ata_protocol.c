#include "ata_protocol.h"
#include "../cf/cf_protocol.h"
#if ((defined CF_USE_PIO_DMA) || (defined HD_USE_DMA))
#include "drivers/dma/atapidma.h"
#endif

ATA_Device_t _ata_dev;
ATA_Device_t *ata_dev = &_ata_dev;
void *ata_dma_buf = NULL;
unsigned long ata_dma_buf_size = 0;
unsigned read_status_count = 1;
extern unsigned ATA_HD_DEVICE_FLAG;
extern unsigned ATA_EIGHT_BIT_ENABLED;
static unsigned char cf_special_flag;

#if ((defined CF_USE_PIO_DMA) || (defined HD_USE_DMA))
static OS_EVENT *atapi_dma_done_event = (OS_EVENT*)0;
#endif

static char * ata_error_string[] = {
	"ATA_NO_ERROR",
	"ATA_ERROR_TIMEOUT",
	"ATA_ERROR_HARDWARE_FAILURE",
	"ATA_ERROR_DEVICE_TYPE",
	"ATA_ERROR_NO_DEVICE"
};

int ata_init_device(ATA_Device_t *ata_dev)
{
    /*_phymem_node_t *memmap = Am_GetSystemMem(MEMMAP_DEFAULT, MEMITEM_DMA_DRAM);
    if(memmap != NULL)
    {
        ata_dma_buf = (void *)(memmap->start);
        ata_dma_buf_size = memmap->end - memmap->start + 1;
    }*/

    //memset(ata_dev, 0, sizeof(ATA_Device_t));
	int error = ATA_NO_ERROR, dev, dev_existed = 0;
	unsigned char reg_data, device_head = 0;

	ata_dev->master_disabled = ATA_MASTER_DISABLED;
	ata_dev->slave_enabled = ATA_SLAVE_ENABLED;
	
#ifdef ATA_DEBUG
	Debug_Printf("Start to poll ATA status, wait BUSY cleared to zero...\n");
#endif
	
	ata_write_reg(ATA_DEVICE_CONTROL_REG, ATA_DEV_CTL_nIEN);
	ata_delay_100ns(ATA_CMD_ISSUE_DELAY);
	
	for(dev=0; dev<2; dev++)
	{
		if(!dev && ata_dev->master_disabled)
			continue;
		else if(dev && !ata_dev->slave_enabled)
			continue;

		if(ata_dev->device_info[dev].device_existed)
		{
			dev_existed = 1;
			continue;
		}
		
		reg_data = dev ? ATA_DRIVE1_MASK : ATA_DRIVE0_MASK;
		ata_write_reg(ATA_DEVICE_HEAD_REG, reg_data);
		ata_delay_ms(2);
		
		error = ata_wait_status_bits(ATA_STATUS_BSY|ATA_STATUS_DRDY, ATA_STATUS_DRDY, ATA_CMD_INIT_TIMEOUT);
		if(error)
		{
			ata_dev->device_info[dev].device_inited = 0;
			continue;
		}
    	
    	ata_write_reg(ATA_SECTOR_COUNT_REG, 0x55);
		ata_delay_100ns(ATA_CMD_ISSUE_DELAY);
		reg_data = ata_read_reg(ATA_SECTOR_COUNT_REG);
		if(reg_data != 0x55)
		{
			ata_dev->device_info[dev].device_inited = 0;
			error = ATA_ERROR_HARDWARE_FAILURE;
			continue;
		}
    	
    	ata_write_reg(ATA_SECTOR_NUMBER_REG, 0xAA);
		ata_delay_100ns(ATA_CMD_ISSUE_DELAY);
		reg_data = ata_read_reg(ATA_SECTOR_NUMBER_REG);
		if(reg_data != 0xAA)
		{
			ata_dev->device_info[dev].device_inited = 0;
			error = ATA_ERROR_HARDWARE_FAILURE;
			continue;
		}

		ata_dev->device_info[dev].device_existed = 1;
		dev_existed = 1;

		if(ATA_EIGHT_BIT_ENABLED)
		{
			device_head = dev ? ATA_DRIVE1_MASK : ATA_DRIVE0_MASK;

			ata_dev->ata_param.Device_Head = device_head;
			ata_dev->ata_param.Features = 0x01;
			ata_dev->ata_param.Command = ATA_SET_FEATURES;
			error = ata_set_features(ata_dev);
			if(error)
				return error;
		}
	}

	if(dev_existed)
	{
#if ((defined CF_USE_PIO_DMA) || (defined HD_USE_DMA))
        atapi_dma_init();
        if (!atapi_dma_done_event) 
            atapi_dma_done_event = AVSemCreate(0);
#endif
        return ATA_NO_ERROR;
    }
    else
		return error;
}

int ata_sw_reset(ATA_Device_t *ata_dev)
{
	int error, dev;

#ifdef ATA_DEBUG
	Debug_Printf("ATA reset processing...\n");
#endif
	
	ata_write_reg(ATA_DEVICE_CONTROL_REG, ATA_DEV_CTL_nIEN | ATA_DEV_CTL_SRST);
	ata_delay_ms(2);
	ata_write_reg(ATA_DEVICE_CONTROL_REG, ATA_DEV_CTL_nIEN);
	ata_delay_ms(2);
	
	for(dev=0; dev<2; dev++)
	{
		if(!ata_dev->device_info[dev].device_existed)
			continue;
		
		ata_write_reg(ATA_DEVICE_HEAD_REG, dev ? ATA_DRIVE1_MASK : ATA_DRIVE0_MASK);
		ata_delay_100ns(ATA_CMD_ISSUE_DELAY);
		
		error = ata_wait_status_bits(ATA_STATUS_BSY|ATA_STATUS_DRDY, ATA_STATUS_DRDY, ATA_CMD_INIT_TIMEOUT);
		if(error)
			return error;
	}
	
	ata_write_reg(ATA_DEVICE_HEAD_REG, ata_dev->current_dev ? ATA_DRIVE1_MASK : ATA_DRIVE0_MASK);
	ata_delay_100ns(ATA_CMD_ISSUE_DELAY);
		
	return ATA_NO_ERROR;
}

int ata_identify_device(ATA_Device_t *ata_dev)
{
	int error = ATA_NO_ERROR, dev, dev_inited = 0, i, j;
	unsigned char device_head = 0;
	unsigned long temp;
	
	for(dev=0; dev<2; dev++)
	{
		if(!ata_dev->device_info[dev].device_existed)
			continue;
		
		if(ata_dev->device_info[dev].device_inited)
		{
			dev_inited = 1;
			continue;
		}

		device_head = dev ? ATA_DRIVE1_MASK : ATA_DRIVE0_MASK;
		device_head |= (ata_dev->current_addr_mode == ATA_LBA_MODE) ? ATA_LBA_MODE : ATA_CHS_MODE;

		ata_dev->ata_param.Device_Head = device_head;
		ata_dev->ata_param.Command = ATA_IDENTIFY_DEVICE;

		//ATA_CMD_RETRY(ata_issue_pio_in_cmd(ata_dev, 1, (void *)&ata_dev->device_info[dev].identify_info), ATA_CMD_DEFAULT_RETRY, error);
		error = ata_issue_pio_in_cmd(ata_dev, 1, (void *)&ata_dev->device_info[dev].identify_info);
		if(error)
		{
			ata_dev->device_info[dev].device_existed = 0;
			continue;
		}

		for(i=0, j=0; i<10; i++)
		{
			ata_dev->device_info[dev].serial_number[j++] = ata_dev->device_info[dev].identify_info.serial_number[i] >> 8;
			ata_dev->device_info[dev].serial_number[j++] = ata_dev->device_info[dev].identify_info.serial_number[i] & 0xFF;
		}
		ata_dev->device_info[dev].serial_number[j] = 0;
		for(i=0, j=0; i<20; i++)
		{
			ata_dev->device_info[dev].model_number[j++] = ata_dev->device_info[dev].identify_info.model_number[i] >> 8;
			ata_dev->device_info[dev].model_number[j++] = ata_dev->device_info[dev].identify_info.model_number[i] & 0xFF;
		}
		ata_dev->device_info[dev].model_number[j] = 0;
		
		temp = ata_dev->device_info[dev].identify_info.total_addressable_sectors[1];
		temp <<= 16;
		temp |= ata_dev->device_info[dev].identify_info.total_addressable_sectors[0];
		ata_dev->device_info[dev].sector_nums = temp;
		ata_dev->device_info[dev].sector_size = ATA_DRQ_BLK_LENGTH_BYTE;
		
		ata_dev->device_info[dev].device_inited = 1;
		dev_inited = 1;
	}
	
	if(dev_inited)
		return ATA_NO_ERROR;
	else
		return error;
}

int ata_select_device(ATA_Device_t *ata_dev, int dev_no)
{
	int error;
	unsigned char device_head;
	
	error = ata_wait_status_bits(ATA_STATUS_BSY|ATA_STATUS_DRQ, 0, ATA_CMD_READY_TIMEOUT);
	if(error)
		return error;
		
	device_head = dev_no ? ATA_DRIVE1_MASK : ATA_DRIVE0_MASK;
	device_head |= (ata_dev->current_addr_mode == ATA_LBA_MODE) ? ATA_LBA_MODE : ATA_CHS_MODE;
		
	ata_write_reg(ATA_DEVICE_HEAD_REG, device_head);
	
	ata_delay_100ns(ATA_CMD_ISSUE_DELAY);
	
	error = ata_wait_status_bits(ATA_STATUS_BSY|ATA_STATUS_DRQ, 0, ATA_CMD_READY_TIMEOUT);
	if(error)
		return error;
	
	ata_dev->current_dev = dev_no;
	
	return ATA_NO_ERROR;
}

#if ((defined CF_USE_PIO_DMA) || (defined HD_USE_DMA))
void atapi_dma_done(void* param){
    AVSemPost(atapi_dma_done_event);
}
#endif

/*This class includes:
-  CFA TRANSLATE SECTOR
-  IDENTIFY DEVICE
-  IDENTIFY PACKET DEVICE
-  READ BUFFER
-  READ MULTIPLE
-  READ SECTOR(S)
-  SMART READ DATA*/
int ata_issue_pio_in_cmd(ATA_Device_t *ata_dev, unsigned long sector_cnt, unsigned char *data_buf)
{
	int error, j;
	unsigned long i;
	unsigned char status, err_reg;
	unsigned short *word_buf;
	unsigned char *byte_buf;
#ifdef CF_USE_PIO_DMA
	INT8U err;
#endif
	
	error = ata_wait_status_bits(ATA_STATUS_BSY|ATA_STATUS_DRQ, 0, ATA_CMD_READY_TIMEOUT);
	if(error)
		return error;
	
	ata_write_reg(ATA_REATURES_REG, ata_dev->ata_param.Features);
	ata_write_reg(ATA_SECTOR_COUNT_REG, ata_dev->ata_param.Sector_Count);
	ata_write_reg(ATA_SECTOR_NUMBER_REG, ata_dev->ata_param.Sector_Number);
	ata_write_reg(ATA_CYLINDER_LOW_REG, ata_dev->ata_param.Cylinder_Low);
	ata_write_reg(ATA_CYLINDER_HIGH_REG, ata_dev->ata_param.Cylinder_High);
	ata_write_reg(ATA_DEVICE_HEAD_REG, ata_dev->ata_param.Device_Head);
	ata_write_reg(ATA_COMMAND_REG, ata_dev->ata_param.Command);

	ata_delay_100ns(ATA_CMD_ISSUE_DELAY);
	
	for(i=0; i<sector_cnt; i++)
	{
		if(ata_dma_buf != NULL && ata_dma_buf_size >= ATA_DRQ_BLK_LENGTH_BYTE)
        {
            word_buf = (void *)(ata_dma_buf);
            byte_buf = ata_dma_buf;
        }
        else
        {
            word_buf = (void *)ata_dev->ata_buf;
            byte_buf = ata_dev->ata_buf;
        }
		
		//This prevents polling host from reading status before it is valid.
		status = ata_read_reg(ATA_ALT_STATUS_REG);

		error = ata_wait_status_bits(ATA_STATUS_BSY|ATA_STATUS_DRQ, ATA_STATUS_DRQ, ATA_CMD_READY_TIMEOUT*3);
		if(error)
		{
		    while(read_status_count--)
		    {
			    status = ata_read_reg(ATA_STATUS_REG);
			    if((status & (ATA_STATUS_BSY|ATA_STATUS_DRQ)) == ATA_STATUS_DRQ)
			    {
			        read_status_count = 1;
			        return error;
			    }
			    else
			        ata_delay_ms(1);
			}
			if(status & ATA_STATUS_ERR)
			{
				err_reg = ata_read_reg(ATA_ERROR_REG);
			}
			return error;
		}

#ifdef CF_USE_PIO_DMA
        atapi_dma_add_work(
            (unsigned)word_buf,       // to/From data location in SDRAM
                0,              // Endian setting: 0,1,2 or 3
                0,              // Level on the Cable select pin during DMA
                0,              // set to 1 if transfering TO the ATAPI device
                0,              // Multi-DMA Mode: 0, 1, 2 or 3 (ignored for UDMA)
                2,              // Type: 0 = Standard DMA, 1 = Ultra DMA, 2 =PIO DMA (new feature)
                0,              // PIO Address: 0,1,..,15.  Used for DMA to PIO address (new feature)
                ATA_DRQ_BLK_LENGTH_BYTE,     // Number of bytes to transfer (must be divisible by 2)
                atapi_dma_done, // callback
                NULL);          // callback arguments  
        AVSemPend(atapi_dma_done_event, 0, &err);
#else
		if (cf_special_flag)
		{
			if(ATA_EIGHT_BIT_ENABLED)
			{
				for(j=0; j<ATA_DRQ_BLK_LENGTH_BYTE; j++)
				{
					*(byte_buf + j) = ata_read_data();
			    	status = ata_read_reg(ATA_STATUS_REG);
				}
			}
			else
			{
				for(j=0; j<ATA_DRQ_BLK_LENGTH_WORD; j++)
				{
					*(word_buf + j) = ata_read_data();
			    	status = ata_read_reg(ATA_STATUS_REG);
				}
			}
		}
		else
		{
			if(ATA_EIGHT_BIT_ENABLED)
			{
				for(j=0; j<ATA_DRQ_BLK_LENGTH_BYTE; j++)
				{
					*(byte_buf + j) = ata_read_data();
				}
			}
			else
			{
				for(j=0; j<ATA_DRQ_BLK_LENGTH_WORD; j++)
				{
					*(word_buf + j) = ata_read_data();
				}
			}
		}
#endif
		
#ifdef AMLOGIC_CHIP_SUPPORT
		if((unsigned long)data_buf == 0x3400000)
		{
			for(j=0; j<ATA_DRQ_BLK_LENGTH_BYTE; j++)
			{
				WRITE_BYTE_TO_FIFO(*byte_buf++);
			}
		}
		else
#endif
		{
			memcpy(data_buf, byte_buf, ATA_DRQ_BLK_LENGTH_BYTE);
			data_buf += ATA_DRQ_BLK_LENGTH_BYTE;
		}
	}

	//This prevents polling host from reading status before it is valid.
	status = ata_read_reg(ATA_ALT_STATUS_REG);
	
	//Status register is read to clear pending interupt.
	status = ata_read_reg(ATA_STATUS_REG);
	
	return ATA_NO_ERROR;
}

/*This class includes:
-  CFA WRITE MULTIPLE WITHOUT ERASE
-  CFA WRITE SECTORS WITHOUT ERASE
-  DOWNLOAD MICROCODE
-  SECURITY DISABLE PASSWORD
-  SECURITY ERASE UNIT
-  SECURITY SET PASSWORD
-  SECURITY UNLOCK
-  WRITE BUFFER
-  WRITE MULTIPLE
-  WRITE SECTOR(S)*/
int ata_issue_pio_out_cmd(ATA_Device_t *ata_dev, unsigned long sector_cnt, unsigned char *data_buf)
{
	int error;
	unsigned long i, j;
	unsigned char status, err_reg;
	unsigned short *word_buf;
	unsigned char *byte_buf;
	
	error = ata_wait_status_bits(ATA_STATUS_BSY|ATA_STATUS_DRQ, 0, ATA_CMD_READY_TIMEOUT);
	if(error)
		return error;
	
	ata_write_reg(ATA_REATURES_REG, ata_dev->ata_param.Features);
	ata_write_reg(ATA_SECTOR_COUNT_REG, ata_dev->ata_param.Sector_Count);
	ata_write_reg(ATA_SECTOR_NUMBER_REG, ata_dev->ata_param.Sector_Number);
	ata_write_reg(ATA_CYLINDER_LOW_REG, ata_dev->ata_param.Cylinder_Low);
	ata_write_reg(ATA_CYLINDER_HIGH_REG, ata_dev->ata_param.Cylinder_High);
	ata_write_reg(ATA_DEVICE_HEAD_REG, ata_dev->ata_param.Device_Head);
	ata_write_reg(ATA_COMMAND_REG, ata_dev->ata_param.Command);

	ata_delay_100ns(ATA_CMD_ISSUE_DELAY);
	
	for(i=0; i<sector_cnt; i++)
	{
		if(ata_dma_buf != NULL && ata_dma_buf_size >= ATA_DRQ_BLK_LENGTH_BYTE)
        {
            word_buf = (void *)(ata_dma_buf);
            byte_buf = ata_dma_buf;
        }
        else
        {
            word_buf = (void *)data_buf;//ata_dev->ata_buf;
            byte_buf = data_buf;//ata_dev->ata_buf;
        }

		//memcpy(byte_buf, data_buf, ATA_DRQ_BLK_LENGTH_BYTE);
		//data_buf += ATA_DRQ_BLK_LENGTH_BYTE;
		
		error = ata_wait_status_bits(ATA_STATUS_BSY|ATA_STATUS_DRQ, ATA_STATUS_DRQ, ATA_CMD_READY_TIMEOUT);
		if(error)
		{
			status = ata_read_reg(ATA_STATUS_REG);
			if(status & ATA_STATUS_ERR)
			{
				err_reg = ata_read_reg(ATA_ERROR_REG);
			}
			return error;
		}

		if(ATA_EIGHT_BIT_ENABLED)
		{
			for(j=0; j<ATA_DRQ_BLK_LENGTH_BYTE; j++)
			{
				ata_write_data((*(byte_buf + j))|0xFF00);
			}
		}
		else
		{
			for(j=0; j<ATA_DRQ_BLK_LENGTH_WORD; j++)
			{
				ata_write_data(*(word_buf + j));
			}
		}
		data_buf += ATA_DRQ_BLK_LENGTH_BYTE;
	}

	//This prevents polling host from reading status before it is valid.
	status = ata_read_reg(ATA_ALT_STATUS_REG);
	
	//Status register is read to clear pending interupt.
	status = ata_read_reg(ATA_STATUS_REG);
	
	return ATA_NO_ERROR;
}

/*This class includes:
-  CFA ERASE SECTORS
-  CFA REQUEST EXTENDED ERROR CODE
-  CHECK POWER MODE
-  FLUSH CACHE
-  GET MEDIA STATUS
-  IDLE
-  IDLE IMMEDIATE
-  INITIALIZE DEVICE PARAMETERS
-  MEDIA EJECT
-  MEDIA LOCK
-  MEDIA UNLOCK
-  NOP
-  READ NATIVE MAX ADDRESS
-  READ VERIFY SECTOR(S)
-  SECURITY ERASE PREPARE
-  SECURITY FREEZE LOCK
-  SEEK
-  SET FEATURES
-  SET MAX ADDRESS
-  SET MULTIPLE MODE
-  SLEEP
-  SMART DISABLE OPERATION
-  SMART ENABLE/DISABLE AUTOSAVE
-  SMART ENABLE OPERATION
-  SMART EXECUTE OFFLINE IMMEDIATE
-  SMART RETURN STATUS
-  STANDBY
-  STANDBY IMMEDIATE*/
int ata_issue_no_data_cmd(ATA_Device_t *ata_dev)
{
	int error;
	unsigned char status, err_reg;
	
	error = ata_wait_status_bits(ATA_STATUS_BSY|ATA_STATUS_DRQ, 0, ATA_CMD_READY_TIMEOUT);
	if(error)
		return error;
	
	ata_write_reg(ATA_REATURES_REG, ata_dev->ata_param.Features);
	ata_write_reg(ATA_SECTOR_COUNT_REG, ata_dev->ata_param.Sector_Count);
	ata_write_reg(ATA_SECTOR_NUMBER_REG, ata_dev->ata_param.Sector_Number);
	ata_write_reg(ATA_CYLINDER_LOW_REG, ata_dev->ata_param.Cylinder_Low);
	ata_write_reg(ATA_CYLINDER_HIGH_REG, ata_dev->ata_param.Cylinder_High);
	ata_write_reg(ATA_DEVICE_HEAD_REG, ata_dev->ata_param.Device_Head);
	ata_write_reg(ATA_COMMAND_REG, ata_dev->ata_param.Command);

	ata_delay_100ns(ATA_CMD_ISSUE_DELAY);
	
	error = ata_wait_status_bits(ATA_STATUS_BSY, 0, ATA_CMD_READY_TIMEOUT);
	if(error)
	{
		status = ata_read_reg(ATA_STATUS_REG);
		if(status & ATA_STATUS_ERR)
		{
			err_reg = ata_read_reg(ATA_ERROR_REG);
		}
		return error;
	}
	
	return ATA_NO_ERROR;
}

#ifdef HD_USE_DMA
int ata_issue_dma_in_cmd(ATA_Device_t *ata_dev, unsigned long sector_cnt, unsigned char *data_buf)
{
    int error;
    unsigned char status, err_reg;
    unsigned short *word_buf;
    INT8U err;

    error = ata_wait_status_bits(ATA_STATUS_BSY|ATA_STATUS_DRQ, 0, ATA_CMD_READY_TIMEOUT);
	if(error)
		return error;

    ata_write_reg(ATA_REATURES_REG, ata_dev->ata_param.Features);
	ata_write_reg(ATA_SECTOR_COUNT_REG, ata_dev->ata_param.Sector_Count);
	ata_write_reg(ATA_SECTOR_NUMBER_REG, ata_dev->ata_param.Sector_Number);
	ata_write_reg(ATA_CYLINDER_LOW_REG, ata_dev->ata_param.Cylinder_Low);
	ata_write_reg(ATA_CYLINDER_HIGH_REG, ata_dev->ata_param.Cylinder_High);
	ata_write_reg(ATA_DEVICE_HEAD_REG, ata_dev->ata_param.Device_Head);
	ata_write_reg(ATA_COMMAND_REG, ata_dev->ata_param.Command);

	ata_delay_100ns(ATA_CMD_ISSUE_DELAY);
	
	status = ata_read_reg(ATA_ALT_STATUS_REG);

	error = ata_wait_status_bits(ATA_STATUS_BSY|ATA_STATUS_DRQ, ATA_STATUS_DRQ, ATA_CMD_READY_TIMEOUT);
	if(error)
	{
		status = ata_read_reg(ATA_STATUS_REG);
		if(status & ATA_STATUS_ERR)
		{
			err_reg = ata_read_reg(ATA_ERROR_REG);
		}
		return error;
	}

    if(ata_dma_buf != NULL && ata_dma_buf_size >= ATA_DRQ_BLK_LENGTH_BYTE*sector_cnt)
    {
        word_buf = (void *)(ata_dma_buf);
    }
    else
    {
        word_buf = (void *)ata_malloc(ATA_DRQ_BLK_LENGTH_BYTE*sector_cnt);
        if(word_buf == NULL)
           word_buf = (void *)data_buf;
    }

    atapi_dma_add_work(
            (unsigned)word_buf,       // to/From data location in SDRAM
            0,              // Endian setting: 0,1,2 or 3
            0,              // Level on the Cable select pin during DMA
            0,              // set to 1 if transfering TO the ATAPI device
            0,              // Multi-DMA Mode: 0, 1, 2 or 3 (ignored for UDMA)
            0,              // Type: 0 = Standard DMA, 1 = Ultra DMA, 2 =PIO DMA (new feature)
            0,              // PIO Address: 0,1,..,15.  Used for DMA to PIO address (new feature)
            ATA_DRQ_BLK_LENGTH_BYTE*sector_cnt,     // Number of bytes to transfer (must be divisible by 2)
            atapi_dma_done, // callback
            NULL);          // callback arguments 
    AVSemPend(atapi_dma_done_event, 0, &err);
    memcpy(data_buf, word_buf, ATA_DRQ_BLK_LENGTH_BYTE*sector_cnt);

    if(ata_dma_buf_size<ATA_DRQ_BLK_LENGTH_BYTE*sector_cnt)
        ata_free(word_buf);

    //This prevents polling host from reading status before it is valid.
	status = ata_read_reg(ATA_ALT_STATUS_REG);
	
	//Status register is read to clear pending interupt.
	status = ata_read_reg(ATA_STATUS_REG);

	return ATA_NO_ERROR;
}
#endif

int ata_wait_status_bits(unsigned char bits_mask, unsigned char bits_value, unsigned long timeout)
{
	unsigned long cnt = 0;
	unsigned char status;

	while(cnt++ < timeout)
	{
		status = ata_read_reg(ATA_STATUS_REG);
		if((status & bits_mask) == bits_value)
			return ATA_NO_ERROR;
		else
		{
#ifdef CONFIG_CF
		    if(!cf_check_insert())
		        return ATA_ERROR_NO_DEVICE;
#endif
			ata_delay_ms(1);		
		}
	}
	
	return ATA_ERROR_TIMEOUT;
}

void ata_clear_ata_param(ATA_Device_t *ata_dev)
{
	ata_dev->ata_param.Features = 0;
	ata_dev->ata_param.Sector_Count = 0;
	ata_dev->ata_param.Sector_Number = 0;
	ata_dev->ata_param.Cylinder_Low = 0;
	ata_dev->ata_param.Cylinder_High = 0;
	ata_dev->ata_param.Device_Head = 0;
	ata_dev->ata_param.Command = 0;
}

char * ata_error_to_string(int errcode)
{
	return ata_error_string[errcode];
}

int ata_read_data_pio(unsigned long lba, unsigned long byte_cnt, unsigned char * data_buf)
{
	int error;
	unsigned long total_sectors, sector_cnt;

	total_sectors = (byte_cnt + ATA_DRQ_BLK_LENGTH_BYTE - 1) / ATA_DRQ_BLK_LENGTH_BYTE;
	
	while(total_sectors)
	{
		sector_cnt = (total_sectors >= 256) ? 256 : total_sectors;
		
		ata_dev->ata_param.Features = 0;
		ata_dev->ata_param.Sector_Count = (sector_cnt == 256) ? 0 : sector_cnt;
		ata_dev->ata_param.Sector_Number = lba & 0xFF;
		ata_dev->ata_param.Cylinder_Low = (lba >> 8) & 0xFF;
		ata_dev->ata_param.Cylinder_High = (lba >> 16) & 0xFF;
		ata_dev->ata_param.Device_Head = (lba >> 24) & 0x0F;
		ata_dev->ata_param.Command = ATA_READ_SECTORS;
		
		ata_dev->ata_param.Device_Head |= ata_dev->current_dev ? ATA_DRIVE1_MASK : ATA_DRIVE0_MASK;
		ata_dev->ata_param.Device_Head |= (ata_dev->current_addr_mode == ATA_LBA_MODE) ? ATA_LBA_MODE : ATA_CHS_MODE;
		
		//ATA_CMD_RETRY(ata_issue_pio_in_cmd(ata_dev, sector_cnt, data_buf), ATA_CMD_DEFAULT_RETRY, error);
		error = ata_issue_pio_in_cmd(ata_dev, sector_cnt, data_buf);
		if(error)
			return error;

		lba += sector_cnt;
		total_sectors -= sector_cnt;
		
#ifdef AMLOGIC_CHIP_SUPPORT
		data_buf += ((unsigned long)data_buf == 0x3400000) ? 0 : (sector_cnt*ATA_DRQ_BLK_LENGTH_BYTE);
#else
		data_buf += (sector_cnt*ATA_DRQ_BLK_LENGTH_BYTE);
#endif
	}

	return ATA_NO_ERROR;
}

int ata_write_data_pio(unsigned long lba, unsigned long byte_cnt, unsigned char * data_buf)
{
	int error;
	unsigned long total_sectors, sector_cnt;

	total_sectors = (byte_cnt + ATA_DRQ_BLK_LENGTH_BYTE - 1) / ATA_DRQ_BLK_LENGTH_BYTE;
	
	while(total_sectors)
	{
		sector_cnt = (total_sectors >= 256) ? 256 : total_sectors;
		
		ata_dev->ata_param.Features = 0;
		ata_dev->ata_param.Sector_Count = (sector_cnt == 256) ? 0 : sector_cnt;
		ata_dev->ata_param.Sector_Number = lba & 0xFF;
		ata_dev->ata_param.Cylinder_Low = (lba >> 8) & 0xFF;
		ata_dev->ata_param.Cylinder_High = (lba >> 16) & 0xFF;
		ata_dev->ata_param.Device_Head = (lba >> 24) & 0x0F;
		ata_dev->ata_param.Command = ATA_WRITE_SECTORS;
		
		ata_dev->ata_param.Device_Head |= ata_dev->current_dev ? ATA_DRIVE1_MASK : ATA_DRIVE0_MASK;
		ata_dev->ata_param.Device_Head |= (ata_dev->current_addr_mode == ATA_LBA_MODE) ? ATA_LBA_MODE : ATA_CHS_MODE;
		
		//ATA_CMD_RETRY(ata_issue_pio_out_cmd(ata_dev, sector_cnt, data_buf), ATA_CMD_DEFAULT_RETRY, error);
		error = ata_issue_pio_out_cmd(ata_dev, sector_cnt, data_buf);
		if(error)
			return error;

		lba += sector_cnt;
		total_sectors -= sector_cnt;
		
		data_buf += (sector_cnt*ATA_DRQ_BLK_LENGTH_BYTE);
	}

	return ATA_NO_ERROR;
}

int ata_check_data_consistency(ATA_Device_t *ata_dev)
{
	int error;
	unsigned char *mbr_buf = (void *)ata_dev->ata_buf;

	cf_special_flag = 0;
    read_status_count = 3000;	
	error = ata_read_data_pio(0, ATA_DRQ_BLK_LENGTH_BYTE, mbr_buf);
	if(error)
		return error;

	if((mbr_buf[510] != 0x55) || (mbr_buf[511] != 0xAA))
	{
		cf_special_flag = 1;
	    //read_status_count = 3000;	
		error = ata_read_data_pio(0, ATA_DRQ_BLK_LENGTH_BYTE, mbr_buf);
		if(error)
			return error;
	}
		
	if((mbr_buf[510] != 0x55) || (mbr_buf[511] != 0xAA))
		return ATA_ERROR_HARDWARE_FAILURE;
		
	return ATA_NO_ERROR;
}

void ata_remove_device(ATA_Device_t *ata_dev, int dev_no)
{
	ata_dev->device_info[dev_no].device_existed = 0;
	ata_dev->device_info[dev_no].device_inited = 0;
}

int ata_check_cmd_validity(ATA_Device_t *ata_dev)
{
	int error;
	unsigned long lba = 0;	// check MBR
	
	ata_dev->ata_param.Features = 0;
	ata_dev->ata_param.Sector_Count = 1;
	ata_dev->ata_param.Sector_Number = lba & 0xFF;
	ata_dev->ata_param.Cylinder_Low = (lba >> 8) & 0xFF;
	ata_dev->ata_param.Cylinder_High = (lba >> 16) & 0xFF;
	ata_dev->ata_param.Device_Head = (lba >> 24) & 0x0F;
	ata_dev->ata_param.Command = ATA_READ_VERIFY_SECTORS;
		
	ata_dev->ata_param.Device_Head |= ata_dev->current_dev ? ATA_DRIVE1_MASK : ATA_DRIVE0_MASK;
	ata_dev->ata_param.Device_Head |= (ata_dev->current_addr_mode == ATA_LBA_MODE) ? ATA_LBA_MODE : ATA_CHS_MODE;
		
	error = ata_issue_no_data_cmd(ata_dev);

	return error;
}

#ifdef HD_USE_DMA
int ata_read_data_dma(unsigned long lba, unsigned long byte_cnt, unsigned char * data_buf)
{
    int error;
	unsigned long total_sectors, sector_nums, data_offset = 0;

	total_sectors = (byte_cnt + ATA_DRQ_BLK_LENGTH_BYTE - 1) / ATA_DRQ_BLK_LENGTH_BYTE;

    while(total_sectors)
    {
        if(total_sectors >= ata_dma_buf_size/512)
            sector_nums = ata_dma_buf_size/512;
        else
            sector_nums = total_sectors;

        data_buf += data_offset;
        lba += data_offset/512;
	    ata_dev->ata_param.Features = 0;
	    ata_dev->ata_param.Sector_Count = (sector_nums == 256) ? 0 : sector_nums;
	    ata_dev->ata_param.Sector_Number = lba & 0xFF;
	    ata_dev->ata_param.Cylinder_Low = (lba >> 8) & 0xFF;
	    ata_dev->ata_param.Cylinder_High = (lba >> 16) & 0xFF;
	    ata_dev->ata_param.Device_Head = (lba >> 24) & 0x0F;
	    ata_dev->ata_param.Command = ATA_READ_DMA;
	
	    ata_dev->ata_param.Device_Head |= ata_dev->current_dev ? ATA_DRIVE1_MASK : ATA_DRIVE0_MASK;
	    ata_dev->ata_param.Device_Head |= (ata_dev->current_addr_mode == ATA_LBA_MODE) ? ATA_LBA_MODE : ATA_CHS_MODE;
    
        ATA_CMD_RETRY(ata_issue_dma_in_cmd(ata_dev, sector_nums, data_buf), ATA_CMD_DEFAULT_RETRY, error);

        if(error)
		    return error;

        total_sectors -= sector_nums;
        data_offset = sector_nums*512;
    }
	
	return ATA_NO_ERROR;
}
#endif

#ifdef HD_USE_DMA
int ata_write_data_dma(unsigned long lba, unsigned long byte_cnt, unsigned char * data_buf)
{
    int error;
	unsigned long total_sectors, sector_nums, data_offset = 0;

	total_sectors = (byte_cnt + ATA_DRQ_BLK_LENGTH_BYTE - 1) / ATA_DRQ_BLK_LENGTH_BYTE;

    while(total_sectors)
    {
        if(total_sectors >= ata_dma_buf_size/512)
            sector_nums = ata_dma_buf_size/512;
        else
            sector_nums = total_sectors;

        data_buf += data_offset;
        lba += data_offset/512;
	    ata_dev->ata_param.Features = 0;
	    ata_dev->ata_param.Sector_Count = (sector_nums == 256) ? 0 : sector_nums;
	    ata_dev->ata_param.Sector_Number = lba & 0xFF;
	    ata_dev->ata_param.Cylinder_Low = (lba >> 8) & 0xFF;
	    ata_dev->ata_param.Cylinder_High = (lba >> 16) & 0xFF;
	    ata_dev->ata_param.Device_Head = (lba >> 24) & 0x0F;
	    ata_dev->ata_param.Command = ATA_WRITE_DMA;
	
	    ata_dev->ata_param.Device_Head |= ata_dev->current_dev ? ATA_DRIVE1_MASK : ATA_DRIVE0_MASK;
	    ata_dev->ata_param.Device_Head |= (ata_dev->current_addr_mode == ATA_LBA_MODE) ? ATA_LBA_MODE : ATA_CHS_MODE;
    
        ATA_CMD_RETRY(ata_issue_dma_out_cmd(ata_dev, sector_nums, data_buf), ATA_CMD_DEFAULT_RETRY, error);

        if(error)
		    return error;

        total_sectors -= sector_nums;
        data_offset = sector_nums*512;
    }
	
	return ATA_NO_ERROR;
}
#endif

#ifdef HD_USE_DMA
int ata_issue_dma_out_cmd(ATA_Device_t *ata_dev, unsigned long sector_cnt, unsigned char *data_buf)
{
    int error;
    unsigned char status, err_reg;
    unsigned short *word_buf;
    INT8U err;

    error = ata_wait_status_bits(ATA_STATUS_BSY|ATA_STATUS_DRQ, 0, ATA_CMD_READY_TIMEOUT);
	if(error)
		return error;

    ata_write_reg(ATA_REATURES_REG, ata_dev->ata_param.Features);
	ata_write_reg(ATA_SECTOR_COUNT_REG, ata_dev->ata_param.Sector_Count);
	ata_write_reg(ATA_SECTOR_NUMBER_REG, ata_dev->ata_param.Sector_Number);
	ata_write_reg(ATA_CYLINDER_LOW_REG, ata_dev->ata_param.Cylinder_Low);
	ata_write_reg(ATA_CYLINDER_HIGH_REG, ata_dev->ata_param.Cylinder_High);
	ata_write_reg(ATA_DEVICE_HEAD_REG, ata_dev->ata_param.Device_Head);
	ata_write_reg(ATA_COMMAND_REG, ata_dev->ata_param.Command);

	ata_delay_100ns(ATA_CMD_ISSUE_DELAY);
	
	status = ata_read_reg(ATA_ALT_STATUS_REG);

	error = ata_wait_status_bits(ATA_STATUS_BSY|ATA_STATUS_DRQ, ATA_STATUS_DRQ, ATA_CMD_READY_TIMEOUT);
	if(error)
	{
		status = ata_read_reg(ATA_STATUS_REG);
		if(status & ATA_STATUS_ERR)
		{
			err_reg = ata_read_reg(ATA_ERROR_REG);
		}
		return error;
	}

    word_buf = (void *)(data_buf);
    atapi_dma_add_work(
            (unsigned)word_buf,       // to/From data location in SDRAM
            0,              // Endian setting: 0,1,2 or 3
            0,              // Level on the Cable select pin during DMA
            1,              // set to 1 if transfering TO the ATAPI device
            0,              // Multi-DMA Mode: 0, 1, 2 or 3 (ignored for UDMA)
            0,              // Type: 0 = Standard DMA, 1 = Ultra DMA, 2 =PIO DMA (new feature)
            0,              // PIO Address: 0,1,..,15.  Used for DMA to PIO address (new feature)
            ATA_DRQ_BLK_LENGTH_BYTE*sector_cnt,     // Number of bytes to transfer (must be divisible by 2)
            atapi_dma_done, // callback
            NULL);          // callback arguments 
    AVSemPend(atapi_dma_done_event, 0, &err);

    //This prevents polling host from reading status before it is valid.
	status = ata_read_reg(ATA_ALT_STATUS_REG);
	
	//Status register is read to clear pending interupt.
	status = ata_read_reg(ATA_STATUS_REG);

	return ATA_NO_ERROR;
}
#endif

int ata_sleep_device(ATA_Device_t *ata_dev)
{
    int error;
	unsigned char device_head;

    device_head = ata_dev->current_dev ? ATA_DRIVE1_MASK : ATA_DRIVE0_MASK;
	device_head |= (ata_dev->current_addr_mode == ATA_LBA_MODE) ? ATA_LBA_MODE : ATA_CHS_MODE;
		
	ata_dev->ata_param.Device_Head = device_head;
	ata_dev->ata_param.Command = ATA_SLEEP;
	
	error = ata_wait_status_bits(ATA_STATUS_BSY|ATA_STATUS_DRQ, 0, ATA_CMD_READY_TIMEOUT);
	if(error)
		return error;

    ata_write_reg(ATA_DEVICE_HEAD_REG, ata_dev->ata_param.Device_Head);
	ata_write_reg(ATA_COMMAND_REG, ata_dev->ata_param.Command);

	ata_delay_100ns(ATA_CMD_ISSUE_DELAY);
	
	error = ata_wait_status_bits(ATA_STATUS_BSY|ATA_STATUS_DRDY, ATA_STATUS_DRDY, ATA_CMD_INIT_TIMEOUT);
	if(error)
        return error;

    return ATA_NO_ERROR;
}

int ata_sw_reset_dev(ATA_Device_t *ata_dev)
{
	int error;

#ifdef ATA_DEBUG
	Debug_Printf("ATA reset processing...\n");
#endif
	
	ata_write_reg(ATA_DEVICE_CONTROL_REG, ATA_DEV_CTL_nIEN | ATA_DEV_CTL_SRST);
	ata_delay_ms(2);
	ata_write_reg(ATA_DEVICE_CONTROL_REG, ATA_DEV_CTL_nIEN);
	ata_delay_ms(2);
		
	ata_write_reg(ATA_DEVICE_HEAD_REG, ata_dev->current_dev ? ATA_DRIVE1_MASK : ATA_DRIVE0_MASK);
	ata_delay_100ns(ATA_CMD_ISSUE_DELAY);
		
	error = ata_wait_status_bits(ATA_STATUS_BSY|ATA_STATUS_DRDY, ATA_STATUS_DRDY, ATA_CMD_INIT_TIMEOUT);
	if(error)
		return error;
	
	ata_write_reg(ATA_DEVICE_HEAD_REG, ata_dev->current_dev? ATA_DRIVE1_MASK : ATA_DRIVE0_MASK);
	ata_delay_100ns(ATA_CMD_ISSUE_DELAY);
		
	return ATA_NO_ERROR;
}

int ata_identify_dev(ATA_Device_t *ata_dev, unsigned char *ata_identify_buf)
{
	int error = ATA_NO_ERROR;
	unsigned char device_head = 0;

	device_head = ata_dev->current_dev ? ATA_DRIVE1_MASK : ATA_DRIVE0_MASK;
	device_head |= (ata_dev->current_addr_mode == ATA_LBA_MODE) ? ATA_LBA_MODE : ATA_CHS_MODE;
			
	ata_dev->ata_param.Device_Head = device_head;
	ata_dev->ata_param.Command = ATA_IDENTIFY_DEVICE;

	error = ata_issue_pio_in_cmd(ata_dev, 1, (void *)ata_identify_buf);
	if(error)
	    return error;

	return ATA_NO_ERROR;
}

int ata_set_features(ATA_Device_t *ata_dev)
{
	int error;
	unsigned char status, err_reg;

    error = ata_wait_status_bits(ATA_STATUS_BSY|ATA_STATUS_DRQ, 0, ATA_CMD_READY_TIMEOUT);
	if(error)
		return error;

    ata_write_reg(ATA_REATURES_REG, ata_dev->ata_param.Features);
	ata_write_reg(ATA_SECTOR_COUNT_REG, ata_dev->ata_param.Sector_Count);
	ata_write_reg(ATA_SECTOR_NUMBER_REG, ata_dev->ata_param.Sector_Number);
	ata_write_reg(ATA_CYLINDER_LOW_REG, ata_dev->ata_param.Cylinder_Low);
	ata_write_reg(ATA_CYLINDER_HIGH_REG, ata_dev->ata_param.Cylinder_High);
	ata_write_reg(ATA_DEVICE_HEAD_REG, ata_dev->ata_param.Device_Head);
	ata_write_reg(ATA_COMMAND_REG, ata_dev->ata_param.Command);

	ata_delay_100ns(ATA_CMD_ISSUE_DELAY);

	status = ata_read_reg(ATA_ALT_STATUS_REG);

	error = ata_wait_status_bits(ATA_STATUS_BSY|ATA_STATUS_DRDY, ATA_STATUS_DRDY, ATA_CMD_READY_TIMEOUT*6);
	if(error)
	{
		status = ata_read_reg(ATA_STATUS_REG);
		if(status & ATA_STATUS_ERR)
		{
			err_reg = ata_read_reg(ATA_ERROR_REG);
		}
		return error;
	}

	return ATA_NO_ERROR;
}
