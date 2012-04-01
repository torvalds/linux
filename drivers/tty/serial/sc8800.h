#ifndef __SC8800_H__
#define __SC8800_H__

typedef struct _spi_packet_head {
	u16 tag; //HEADER_TAG(0x7e7f) 
	u16 type; //HEADER_TYPE(0xaa55) 
	u32 length; //the length of data after head  (8192-128 bytes) 
	u32 frame_num; //no used , always 0
	u32 reserved2; //reserved 
} SPI_PACKET_HEAD_T;


/*define flatform data struct*/
struct plat_sc8800 {
	int slav_rts_pin;
	int slav_rdy_pin;
	int master_rts_pin;
	int master_rdy_pin;
	int poll_time;
	int (*io_init)(void);
	int (*io_deinit)(void);
};

#endif
