#ifndef _HALMAC_TX_BD_AP_H_
#define _HALMAC_TX_BD_AP_H_
#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT || HALMAC_8188F_SUPPORT)

/*TXBD_DW0*/

#define SET_TX_BD_OWN(__pTxBd, __Value)    HALMAC_SET_BD_FIELD_CLR(((PHALMAC_TX_BD)__pTxBd)->Dword0, __Value, 0x1, 31)
#define SET_TX_BD_OWN_NO_CLR(__pTxBd, __Value)    HALMAC_SET_BD_FIELD_NO_CLR(((PHALMAC_TX_BD)__pTxBd)->Dword0, __Value, 0x1, 31)
#define GET_TX_BD_OWN(__pTxBd)    HALMAC_GET_BD_FIELD(((PHALMAC_TX_BD)__pTxBd)->Dword0, 0x1, 31)
#define SET_TX_BD_PSB(__pTxBd, __Value)    HALMAC_SET_BD_FIELD_CLR(((PHALMAC_TX_BD)__pTxBd)->Dword0, __Value, 0xff, 16)
#define SET_TX_BD_PSB_NO_CLR(__pTxBd, __Value)    HALMAC_SET_BD_FIELD_NO_CLR(((PHALMAC_TX_BD)__pTxBd)->Dword0, __Value, 0xff, 16)
#define GET_TX_BD_PSB(__pTxBd)    HALMAC_GET_BD_FIELD(((PHALMAC_TX_BD)__pTxBd)->Dword0, 0xff, 16)
#define SET_TX_BD_TX_BUFF_SIZE0(__pTxBd, __Value)    HALMAC_SET_BD_FIELD_CLR(((PHALMAC_TX_BD)__pTxBd)->Dword0, __Value, 0xffff, 0)
#define SET_TX_BD_TX_BUFF_SIZE0_NO_CLR(__pTxBd, __Value)    HALMAC_SET_BD_FIELD_NO_CLR(((PHALMAC_TX_BD)__pTxBd)->Dword0, __Value, 0xffff, 0)
#define GET_TX_BD_TX_BUFF_SIZE0(__pTxBd)    HALMAC_GET_BD_FIELD(((PHALMAC_TX_BD)__pTxBd)->Dword0, 0xffff, 0)

/*TXBD_DW1*/

#define SET_TX_BD_PHYSICAL_ADDR0_LOW(__pTxBd, __Value)    HALMAC_SET_BD_FIELD_CLR(((PHALMAC_TX_BD)__pTxBd)->Dword1, __Value, 0xffffffff, 0)
#define SET_TX_BD_PHYSICAL_ADDR0_LOW_NO_CLR(__pTxBd, __Value)    HALMAC_SET_BD_FIELD_NO_CLR(((PHALMAC_TX_BD)__pTxBd)->Dword1, __Value, 0xffffffff, 0)
#define GET_TX_BD_PHYSICAL_ADDR0_LOW(__pTxBd)    HALMAC_GET_BD_FIELD(((PHALMAC_TX_BD)__pTxBd)->Dword1, 0xffffffff, 0)

/*TXBD_DW2*/

#define SET_TX_BD_PHYSICAL_ADDR0_HIGH(__pTxBd, __Value)    HALMAC_SET_BD_FIELD_CLR(((PHALMAC_TX_BD)__pTxBd)->Dword2, __Value, 0xffffffff, 0)
#define SET_TX_BD_PHYSICAL_ADDR0_HIGH_NO_CLR(__pTxBd, __Value)    HALMAC_SET_BD_FIELD_NO_CLR(((PHALMAC_TX_BD)__pTxBd)->Dword2, __Value, 0xffffffff, 0)
#define GET_TX_BD_PHYSICAL_ADDR0_HIGH(__pTxBd)    HALMAC_GET_BD_FIELD(((PHALMAC_TX_BD)__pTxBd)->Dword2, 0xffffffff, 0)

/*TXBD_DW4*/

#define SET_TX_BD_A1(__pTxBd, __Value)    HALMAC_SET_BD_FIELD_CLR(((PHALMAC_TX_BD)__pTxBd)->Dword4, __Value, 0x1, 31)
#define SET_TX_BD_A1_NO_CLR(__pTxBd, __Value)    HALMAC_SET_BD_FIELD_NO_CLR(((PHALMAC_TX_BD)__pTxBd)->Dword4, __Value, 0x1, 31)
#define GET_TX_BD_A1(__pTxBd)    HALMAC_GET_BD_FIELD(((PHALMAC_TX_BD)__pTxBd)->Dword4, 0x1, 31)
#define SET_TX_BD_TX_BUFF_SIZE1(__pTxBd, __Value)    HALMAC_SET_BD_FIELD_CLR(((PHALMAC_TX_BD)__pTxBd)->Dword4, __Value, 0xffff, 0)
#define SET_TX_BD_TX_BUFF_SIZE1_NO_CLR(__pTxBd, __Value)    HALMAC_SET_BD_FIELD_NO_CLR(((PHALMAC_TX_BD)__pTxBd)->Dword4, __Value, 0xffff, 0)
#define GET_TX_BD_TX_BUFF_SIZE1(__pTxBd)    HALMAC_GET_BD_FIELD(((PHALMAC_TX_BD)__pTxBd)->Dword4, 0xffff, 0)

/*TXBD_DW5*/

#define SET_TX_BD_PHYSICAL_ADDR1_LOW(__pTxBd, __Value)    HALMAC_SET_BD_FIELD_CLR(((PHALMAC_TX_BD)__pTxBd)->Dword5, __Value, 0xffffffff, 0)
#define SET_TX_BD_PHYSICAL_ADDR1_LOW_NO_CLR(__pTxBd, __Value)    HALMAC_SET_BD_FIELD_NO_CLR(((PHALMAC_TX_BD)__pTxBd)->Dword5, __Value, 0xffffffff, 0)
#define GET_TX_BD_PHYSICAL_ADDR1_LOW(__pTxBd)    HALMAC_GET_BD_FIELD(((PHALMAC_TX_BD)__pTxBd)->Dword5, 0xffffffff, 0)

/*TXBD_DW6*/

#define SET_TX_BD_PHYSICAL_ADDR1_HIGH(__pTxBd, __Value)    HALMAC_SET_BD_FIELD_CLR(((PHALMAC_TX_BD)__pTxBd)->Dword6, __Value, 0xffffffff, 0)
#define SET_TX_BD_PHYSICAL_ADDR1_HIGH_NO_CLR(__pTxBd, __Value)    HALMAC_SET_BD_FIELD_NO_CLR(((PHALMAC_TX_BD)__pTxBd)->Dword6, __Value, 0xffffffff, 0)
#define GET_TX_BD_PHYSICAL_ADDR1_HIGH(__pTxBd)    HALMAC_GET_BD_FIELD(((PHALMAC_TX_BD)__pTxBd)->Dword6, 0xffffffff, 0)

/*TXBD_DW8*/

#define SET_TX_BD_A2(__pTxBd, __Value)    HALMAC_SET_BD_FIELD_CLR(((PHALMAC_TX_BD)__pTxBd)->Dword8, __Value, 0x1, 31)
#define SET_TX_BD_A2_NO_CLR(__pTxBd, __Value)    HALMAC_SET_BD_FIELD_NO_CLR(((PHALMAC_TX_BD)__pTxBd)->Dword8, __Value, 0x1, 31)
#define GET_TX_BD_A2(__pTxBd)    HALMAC_GET_BD_FIELD(((PHALMAC_TX_BD)__pTxBd)->Dword8, 0x1, 31)
#define SET_TX_BD_TX_BUFF_SIZE2(__pTxBd, __Value)    HALMAC_SET_BD_FIELD_CLR(((PHALMAC_TX_BD)__pTxBd)->Dword8, __Value, 0xffff, 0)
#define SET_TX_BD_TX_BUFF_SIZE2_NO_CLR(__pTxBd, __Value)    HALMAC_SET_BD_FIELD_NO_CLR(((PHALMAC_TX_BD)__pTxBd)->Dword8, __Value, 0xffff, 0)
#define GET_TX_BD_TX_BUFF_SIZE2(__pTxBd)    HALMAC_GET_BD_FIELD(((PHALMAC_TX_BD)__pTxBd)->Dword8, 0xffff, 0)

/*TXBD_DW9*/

#define SET_TX_BD_PHYSICAL_ADDR2_LOW(__pTxBd, __Value)    HALMAC_SET_BD_FIELD_CLR(((PHALMAC_TX_BD)__pTxBd)->Dword9, __Value, 0xffffffff, 0)
#define SET_TX_BD_PHYSICAL_ADDR2_LOW_NO_CLR(__pTxBd, __Value)    HALMAC_SET_BD_FIELD_NO_CLR(((PHALMAC_TX_BD)__pTxBd)->Dword9, __Value, 0xffffffff, 0)
#define GET_TX_BD_PHYSICAL_ADDR2_LOW(__pTxBd)    HALMAC_GET_BD_FIELD(((PHALMAC_TX_BD)__pTxBd)->Dword9, 0xffffffff, 0)

/*TXBD_DW10*/

#define SET_TX_BD_PHYSICAL_ADDR2_HIGH(__pTxBd, __Value)    HALMAC_SET_BD_FIELD_CLR(((PHALMAC_TX_BD)__pTxBd)->Dword10, __Value, 0xffffffff, 0)
#define SET_TX_BD_PHYSICAL_ADDR2_HIGH_NO_CLR(__pTxBd, __Value)    HALMAC_SET_BD_FIELD_NO_CLR(((PHALMAC_TX_BD)__pTxBd)->Dword10, __Value, 0xffffffff, 0)
#define GET_TX_BD_PHYSICAL_ADDR2_HIGH(__pTxBd)    HALMAC_GET_BD_FIELD(((PHALMAC_TX_BD)__pTxBd)->Dword10, 0xffffffff, 0)

/*TXBD_DW12*/

#define SET_TX_BD_A3(__pTxBd, __Value)    HALMAC_SET_BD_FIELD_CLR(((PHALMAC_TX_BD)__pTxBd)->Dword12, __Value, 0x1, 31)
#define SET_TX_BD_A3_NO_CLR(__pTxBd, __Value)    HALMAC_SET_BD_FIELD_NO_CLR(((PHALMAC_TX_BD)__pTxBd)->Dword12, __Value, 0x1, 31)
#define GET_TX_BD_A3(__pTxBd)    HALMAC_GET_BD_FIELD(((PHALMAC_TX_BD)__pTxBd)->Dword12, 0x1, 31)
#define SET_TX_BD_TX_BUFF_SIZE3(__pTxBd, __Value)    HALMAC_SET_BD_FIELD_CLR(((PHALMAC_TX_BD)__pTxBd)->Dword12, __Value, 0xffff, 0)
#define SET_TX_BD_TX_BUFF_SIZE3_NO_CLR(__pTxBd, __Value)    HALMAC_SET_BD_FIELD_NO_CLR(((PHALMAC_TX_BD)__pTxBd)->Dword12, __Value, 0xffff, 0)
#define GET_TX_BD_TX_BUFF_SIZE3(__pTxBd)    HALMAC_GET_BD_FIELD(((PHALMAC_TX_BD)__pTxBd)->Dword12, 0xffff, 0)

/*TXBD_DW13*/

#define SET_TX_BD_PHYSICAL_ADDR3_LOW(__pTxBd, __Value)    HALMAC_SET_BD_FIELD_CLR(((PHALMAC_TX_BD)__pTxBd)->Dword13, __Value, 0xffffffff, 0)
#define SET_TX_BD_PHYSICAL_ADDR3_LOW_NO_CLR(__pTxBd, __Value)    HALMAC_SET_BD_FIELD_NO_CLR(((PHALMAC_TX_BD)__pTxBd)->Dword13, __Value, 0xffffffff, 0)
#define GET_TX_BD_PHYSICAL_ADDR3_LOW(__pTxBd)    HALMAC_GET_BD_FIELD(((PHALMAC_TX_BD)__pTxBd)->Dword13, 0xffffffff, 0)

/*TXBD_DW14*/

#define SET_TX_BD_PHYSICAL_ADDR3_HIGH(__pTxBd, __Value)    HALMAC_SET_BD_FIELD_CLR(((PHALMAC_TX_BD)__pTxBd)->Dword14, __Value, 0xffffffff, 0)
#define SET_TX_BD_PHYSICAL_ADDR3_HIGH_NO_CLR(__pTxBd, __Value)    HALMAC_SET_BD_FIELD_NO_CLR(((PHALMAC_TX_BD)__pTxBd)->Dword14, __Value, 0xffffffff, 0)
#define GET_TX_BD_PHYSICAL_ADDR3_HIGH(__pTxBd)    HALMAC_GET_BD_FIELD(((PHALMAC_TX_BD)__pTxBd)->Dword14, 0xffffffff, 0)

#endif


#endif
