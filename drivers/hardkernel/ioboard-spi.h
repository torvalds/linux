//[*]--------------------------------------------------------------------------------------------------[*]
//
//
// 
//  IOBOARD_SPI driver
//  2013.07.17
// 
//
//[*]--------------------------------------------------------------------------------------------------[*]
#ifndef _IOBOARD_SPI_H_
#define _IOBOARD_SPI_H_

//[*]--------------------------------------------------------------------------------------------------[*]
struct ioboard_spi_iocreg	{
    unsigned char   cmd;
    unsigned int    addr;
    unsigned int    size;
	unsigned char	*rwdata;
} 	__attribute__ ((packed));

#define IOBOARD_CMD_SPI_WRITE   0
// iocreg.cmd       = IOBOARD_CMD_SPI_WRITE
// iocreg.addr      = start addr
// iocreg.size      = write size
// iocreg.rw_data   = write buffer (write buffer size >= write size)

#define IOBOARD_CMD_SPI_READ    1
// iocreg.cmd       = IOBOARD_CMD_SPI_READ
// iocreg.addr      = start addr
// iocreg.size      = read size
// iocreg.rw_data   = read buffer (read buffer size >= read size)

#define IOBOARD_CMD_SPI_ERASE   2
// iocreg.cmd       = IOBOARD_CMD_SPI_ERASE
// iocreg.addr      = if(4K, 32K, 64K -> start address)
// iocreg.size      = ERASE_SIZE_ALL(erase size)
// iocreg.rw_data   = don't care
    // erase size value
    #define ERASE_SIZE_4K       0x20    
    #define ERASE_SIZE_32K      0x52
    #define ERASE_SIZE_64K      0xD8
    #define ERASE_SIZE_ALL      0xC7

#define IOBOARD_IOCGREG     _IOR('i', 1, struct ioboard_spi_iocreg *)
#define IOBOARD_IOCSREG     _IOW('i', 2, struct ioboard_spi_iocreg *)
#define IOBOARD_IOCGSTATUS  _IOW('i', 3, unsigned char *)

//[*]--------------------------------------------------------------------------------------------------[*]
struct ioboard_spi  {
	struct miscdevice       *misc;
    struct spi_device       *spi;
};

//[*]--------------------------------------------------------------------------------------------------[*]
extern  int ioboard_spi_erase        (struct spi_device *spi, unsigned int addr, unsigned char mode);
extern  int ioboard_spi_write   (struct spi_device *spi, unsigned int addr, unsigned char *wdata, unsigned int size);
extern  int ioboard_spi_read    (struct spi_device *spi, unsigned int addr, unsigned char *rdata, unsigned int size);
//[*]--------------------------------------------------------------------------------------------------[*]
extern  void 	ioboard_spi_misc_remove	(struct device *dev);
extern  int		ioboard_spi_misc_probe	(struct spi_device *spi);

//[*]--------------------------------------------------------------------------------------------------[*]
#endif  // _IOBOARD_SPI_H_
//[*]--------------------------------------------------------------------------------------------------[*]
//[*]--------------------------------------------------------------------------------------------------[*]
