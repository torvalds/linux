#ifndef BCM_PHS_DEFINES_H
#define BCM_PHS_DEFINES_H

#define PHS_INVALID_TABLE_INDEX 0xffffffff

/************************* MACROS **********************************************/
#define PHS_MEM_TAG "_SHP"



//PHS Defines
#define STATUS_PHS_COMPRESSED       0xa1
#define STATUS_PHS_NOCOMPRESSION    0xa2
#define APPLY_PHS					1
#define MAX_NO_BIT					7
#define ZERO_PHSI					0
#define VERIFY						0
#define SIZE_MULTIPLE_32             4
#define UNCOMPRESSED_PACKET			 0
#define DYNAMIC         			 0
#define SUPPRESS					 0x80
#define NO_CLASSIFIER_MATCH			 0
#define SEND_PACKET_UNCOMPRESSED	 0
#define PHSI_IS_ZERO				 0
#define PHSI_LEN					 1
#define ERROR_LEN					 0
#define PHS_BUFFER_SIZE				 1532


//#define MAX_PHS_LENGTHS 100
#define MAX_PHSRULE_PER_SF       20
#define MAX_SERVICEFLOWS			 17

//PHS Error Defines
#define PHS_SUCCESS                       0
#define ERR_PHS_INVALID_DEVICE_EXETENSION  0x800
#define ERR_PHS_INVALID_PHS_RULE           0x801
#define ERR_PHS_RULE_ALREADY_EXISTS        0x802
#define ERR_SF_MATCH_FAIL                  0x803
#define ERR_INVALID_CLASSIFIERTABLE_FOR_SF 0x804
#define ERR_SFTABLE_FULL                   0x805
#define ERR_CLSASSIFIER_TABLE_FULL         0x806
#define ERR_PHSRULE_MEMALLOC_FAIL          0x807
#define ERR_CLSID_MATCH_FAIL               0x808
#define ERR_PHSRULE_MATCH_FAIL             0x809

typedef struct _S_PHS_RULE
{
    /// brief 8bit PHSI Of The Service Flow
    B_UINT8                         u8PHSI;
    /// brief PHSF Of The Service Flow
    B_UINT8                         u8PHSFLength;
    B_UINT8                         u8PHSF[MAX_PHS_LENGTHS];
    /// brief PHSM Of The Service Flow
    B_UINT8                         u8PHSMLength;
    B_UINT8                         u8PHSM[MAX_PHS_LENGTHS];
    /// brief 8bit PHSS Of The Service Flow
    B_UINT8                         u8PHSS;
    /// brief 8bit PHSV Of The Service Flow
    B_UINT8                         u8PHSV;
   //Reference Count for this PHS Rule
    B_UINT8                         u8RefCnt;
   //Flag to Store Unclassified PHS rules only in DL
    B_UINT8							bUnclassifiedPHSRule;

	B_UINT8							u8Reserved[3];

  	LONG							PHSModifiedBytes;
	ULONG       				PHSModifiedNumPackets;
	ULONG           			PHSErrorNumPackets;
}S_PHS_RULE;


typedef enum _E_CLASSIFIER_ENTRY_CONTEXT
{
	eActiveClassifierRuleContext,
	eOldClassifierRuleContext
}E_CLASSIFIER_ENTRY_CONTEXT;

typedef struct _S_CLASSIFIER_ENTRY
{
	B_UINT8  bUsed;
	B_UINT16 uiClassifierRuleId;
	B_UINT8  u8PHSI;
	S_PHS_RULE *pstPhsRule;
	B_UINT8	bUnclassifiedPHSRule;

}S_CLASSIFIER_ENTRY;


typedef struct _S_CLASSIFIER_TABLE
{
	B_UINT16 uiTotalClassifiers;
	S_CLASSIFIER_ENTRY stActivePhsRulesList[MAX_PHSRULE_PER_SF];
	S_CLASSIFIER_ENTRY stOldPhsRulesList[MAX_PHSRULE_PER_SF];
	B_UINT16    uiOldestPhsRuleIndex;

}S_CLASSIFIER_TABLE;


typedef struct _S_SERVICEFLOW_ENTRY
{
	B_UINT8		bUsed;
	B_UINT16    uiVcid;
	S_CLASSIFIER_TABLE  *pstClassifierTable;
}S_SERVICEFLOW_ENTRY;

typedef struct _S_SERVICEFLOW_TABLE
{
	B_UINT16 uiTotalServiceFlows;
	S_SERVICEFLOW_ENTRY stSFList[MAX_SERVICEFLOWS];

}S_SERVICEFLOW_TABLE;


typedef struct _PHS_DEVICE_EXTENSION
{
	/* PHS Specific data*/
	S_SERVICEFLOW_TABLE *pstServiceFlowPhsRulesTable;
	void   *CompressedTxBuffer;
	void   *UnCompressedRxBuffer;
}PHS_DEVICE_EXTENSION,*PPHS_DEVICE_EXTENSION;


#endif
