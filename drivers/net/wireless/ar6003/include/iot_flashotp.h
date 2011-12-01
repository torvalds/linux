#ifndef _IOT_FLASHOTP_H_
#define _IOT_FLASHOTP_H_
/******************************************/
/* firmware input param value definitions */
/******************************************/
/* IOTFLASHOTP_PARAM_SKIP_OTP - instructs firmware to skip OTP operations */
#define IOTFLASHOTP_PARAM_SKIP_OTP                  (0x00000001)
/* IOTFLASHOTP_PARAM_SKIP_FLASH - instructs firmware to skip Flash operations */
#define IOTFLASHOTP_PARAM_SKIP_FLASH                (0x00000002)
/* IOTFLASHOTP_PARAM_USE_NVRAM_CONFIG_FROM_OTP - instructs firmware to use NVRAM config found in OTP
 *  to access flash. */
#define IOTFLASHOTP_PARAM_USE_NVRAM_CONFIG_FROM_OTP (0x00000004)
/*************************************/
/* firmware return value definitions */
/*************************************/
#define IOTFLASHOTP_RESULT_OTP_SUCCESS              (0x00000001)
#define IOTFLASHOTP_RESULT_OTP_FAILED               (0x00000002)
#define IOTFLASHOTP_RESULT_OTP_NOT_WRITTEN          (0x00000004)
#define IOTFLASHOTP_RESULT_OTP_SKIPPED              (0x00000008)
#define IOTFLASHOTP_RESULT_OTP_POS_MASK             (0x0000000f)
#define IOTFLASHOTP_RESULT_OTP_POS_SHIFT            (8)
#define IOTFLASHOTP_RESULT_OTP_MASK                 (0x0000ffff)

#define IOTFLASHOTP_RESULT_FLASH_SUCCESS            (0x00010000)
#define IOTFLASHOTP_RESULT_FLASH_FAILED             (0x00020000)
#define IOTFLASHOTP_RESULT_FLASH_VALIDATE_FAILED    (0x00040000)
#define IOTFLASHOTP_RESULT_FLASH_SKIPPED            (0x00080000)
#define IOTFLASHOTP_RESULT_FLASH_MASK               (0xffff0000)


#if !defined(AR6002_REV4)
typedef unsigned long A_UINT32;
#endif

typedef struct{
    A_UINT32 length; // length of binary in bytes
    A_UINT32 loadAddr; // address to which the image should be loaded
    A_UINT32 execAddr; // address from which execution should start
    A_UINT32 MACOffset; // offset in image of MAC address location.
    A_UINT32 ChksumOffset; // offset of checksum location <0 implies no checksum>
    A_UINT32 ChksumStart; // offset in image for start of checksum calculation 
    A_UINT32 ChksumEnd; // offset in image for end of checksum calculation
    A_UINT32 partitionTableOffset; // offset in image for start of 3 word partition table
    A_UINT32 flashDescOffset; // offset in image for the start of the flash descriptor file
}CONFIG_HEADER;

typedef struct{
    A_UINT32 capacity; // size of flash chip in bytes
    A_UINT32 blocksize; // size of a block needed for nvram block erase command
    A_UINT32 sectorsize; // size of sector needed for nvram sector erase command
    A_UINT32 pagesize; // size of page needed for nvram write command
}FLASH_DESCRIPTOR;


#endif /* _IOT_FLASHOTP_H_ */
