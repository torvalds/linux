#ifndef _MIROPCM20_RDS_CORE_H_
#define _MIROPCM20_RDS_CORE_H_

extern int aci_rds_cmd(unsigned char cmd, unsigned char databuffer[], int datasize);

#define RDS_STATUS      0x01
#define RDS_STATIONNAME 0x02
#define RDS_TEXT        0x03
#define RDS_ALTFREQ     0x04
#define RDS_TIMEDATE    0x05
#define RDS_PI_CODE     0x06
#define RDS_PTYTATP     0x07
#define RDS_RESET       0x08
#define RDS_RXVALUE     0x09

extern void __exit unload_aci_rds(void);
extern int __init attach_aci_rds(void);

#endif /* _MIROPCM20_RDS_CORE_H_ */
