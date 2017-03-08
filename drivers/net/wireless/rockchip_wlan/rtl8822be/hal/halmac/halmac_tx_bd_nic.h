#ifndef _HALMAC_TX_BD_NIC_H_
#define _HALMAC_TX_BD_NIC_H_
#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT || HALMAC_8188F_SUPPORT)

/*TXBD_DW0*/

#define SET_TX_BD_OWN(__pTxBd, __Value)    SET_BITS_TO_LE_4BYTE(__pTxBd + 0x00, 31, 1, __Value)
#define GET_TX_BD_OWN(__pTxBd)    LE_BITS_TO_4BYTE(__pTxBd + 0x00, 31, 1)
#define SET_TX_BD_PSB(__pTxBd, __Value)    SET_BITS_TO_LE_4BYTE(__pTxBd + 0x00, 16, 8, __Value)
#define GET_TX_BD_PSB(__pTxBd)    LE_BITS_TO_4BYTE(__pTxBd + 0x00, 16, 8)
#define SET_TX_BD_TX_BUFF_SIZE0(__pTxBd, __Value)    SET_BITS_TO_LE_4BYTE(__pTxBd + 0x00, 0, 16, __Value)
#define GET_TX_BD_TX_BUFF_SIZE0(__pTxBd)    LE_BITS_TO_4BYTE(__pTxBd + 0x00, 0, 16)

/*TXBD_DW1*/

#define SET_TX_BD_PHYSICAL_ADDR0_LOW(__pTxBd, __Value)    SET_BITS_TO_LE_4BYTE(__pTxBd + 0x04, 0, 32, __Value)
#define GET_TX_BD_PHYSICAL_ADDR0_LOW(__pTxBd)    LE_BITS_TO_4BYTE(__pTxBd + 0x04, 0, 32)

/*TXBD_DW2*/

#define SET_TX_BD_PHYSICAL_ADDR0_HIGH(__pTxBd, __Value)    SET_BITS_TO_LE_4BYTE(__pTxBd + 0x08, 0, 32, __Value)
#define GET_TX_BD_PHYSICAL_ADDR0_HIGH(__pTxBd)    LE_BITS_TO_4BYTE(__pTxBd + 0x08, 0, 32)

/*TXBD_DW4*/

#define SET_TX_BD_A1(__pTxBd, __Value)    SET_BITS_TO_LE_4BYTE(__pTxBd + 0x10, 31, 1, __Value)
#define GET_TX_BD_A1(__pTxBd)    LE_BITS_TO_4BYTE(__pTxBd + 0x10, 31, 1)
#define SET_TX_BD_TX_BUFF_SIZE1(__pTxBd, __Value)    SET_BITS_TO_LE_4BYTE(__pTxBd + 0x10, 0, 16, __Value)
#define GET_TX_BD_TX_BUFF_SIZE1(__pTxBd)    LE_BITS_TO_4BYTE(__pTxBd + 0x10, 0, 16)

/*TXBD_DW5*/

#define SET_TX_BD_PHYSICAL_ADDR1_LOW(__pTxBd, __Value)    SET_BITS_TO_LE_4BYTE(__pTxBd + 0x14, 0, 32, __Value)
#define GET_TX_BD_PHYSICAL_ADDR1_LOW(__pTxBd)    LE_BITS_TO_4BYTE(__pTxBd + 0x14, 0, 32)

/*TXBD_DW6*/

#define SET_TX_BD_PHYSICAL_ADDR1_HIGH(__pTxBd, __Value)    SET_BITS_TO_LE_4BYTE(__pTxBd + 0x18, 0, 32, __Value)
#define GET_TX_BD_PHYSICAL_ADDR1_HIGH(__pTxBd)    LE_BITS_TO_4BYTE(__pTxBd + 0x18, 0, 32)

/*TXBD_DW8*/

#define SET_TX_BD_A2(__pTxBd, __Value)    SET_BITS_TO_LE_4BYTE(__pTxBd + 0x20, 31, 1, __Value)
#define GET_TX_BD_A2(__pTxBd)    LE_BITS_TO_4BYTE(__pTxBd + 0x20, 31, 1)
#define SET_TX_BD_TX_BUFF_SIZE2(__pTxBd, __Value)    SET_BITS_TO_LE_4BYTE(__pTxBd + 0x20, 0, 16, __Value)
#define GET_TX_BD_TX_BUFF_SIZE2(__pTxBd)    LE_BITS_TO_4BYTE(__pTxBd + 0x20, 0, 16)

/*TXBD_DW9*/

#define SET_TX_BD_PHYSICAL_ADDR2_LOW(__pTxBd, __Value)    SET_BITS_TO_LE_4BYTE(__pTxBd + 0x24, 0, 32, __Value)
#define GET_TX_BD_PHYSICAL_ADDR2_LOW(__pTxBd)    LE_BITS_TO_4BYTE(__pTxBd + 0x24, 0, 32)

/*TXBD_DW10*/

#define SET_TX_BD_PHYSICAL_ADDR2_HIGH(__pTxBd, __Value)    SET_BITS_TO_LE_4BYTE(__pTxBd + 0x28, 0, 32, __Value)
#define GET_TX_BD_PHYSICAL_ADDR2_HIGH(__pTxBd)    LE_BITS_TO_4BYTE(__pTxBd + 0x28, 0, 32)

/*TXBD_DW12*/

#define SET_TX_BD_A3(__pTxBd, __Value)    SET_BITS_TO_LE_4BYTE(__pTxBd + 0x30, 31, 1, __Value)
#define GET_TX_BD_A3(__pTxBd)    LE_BITS_TO_4BYTE(__pTxBd + 0x30, 31, 1)
#define SET_TX_BD_TX_BUFF_SIZE3(__pTxBd, __Value)    SET_BITS_TO_LE_4BYTE(__pTxBd + 0x30, 0, 16, __Value)
#define GET_TX_BD_TX_BUFF_SIZE3(__pTxBd)    LE_BITS_TO_4BYTE(__pTxBd + 0x30, 0, 16)

/*TXBD_DW13*/

#define SET_TX_BD_PHYSICAL_ADDR3_LOW(__pTxBd, __Value)    SET_BITS_TO_LE_4BYTE(__pTxBd + 0x34, 0, 32, __Value)
#define GET_TX_BD_PHYSICAL_ADDR3_LOW(__pTxBd)    LE_BITS_TO_4BYTE(__pTxBd + 0x34, 0, 32)

/*TXBD_DW14*/

#define SET_TX_BD_PHYSICAL_ADDR3_HIGH(__pTxBd, __Value)    SET_BITS_TO_LE_4BYTE(__pTxBd + 0x38, 0, 32, __Value)
#define GET_TX_BD_PHYSICAL_ADDR3_HIGH(__pTxBd)    LE_BITS_TO_4BYTE(__pTxBd + 0x38, 0, 32)

#endif


#endif
