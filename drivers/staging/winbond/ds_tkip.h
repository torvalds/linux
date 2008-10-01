// Rotation functions on 32 bit values
#define ROL32( A, n ) \
    ( ((A) << (n)) | ( ((A)>>(32-(n)))  & ( (1UL << (n)) - 1 ) ) )

#define ROR32( A, n )   ROL32( (A), 32-(n) )


typedef struct tkip
{
    u32	K0, K1;		// Key
	union
	{
		struct // Current state
		{
			u32	L;
			u32	R;
		};
		u8	LR[8];
	};
	union
	{
		u32	M;		// Message accumulator (single word)
		u8	Mb[4];
	};
	s32		bytes_in_M;	// # bytes in M
} tkip_t;

//void _append_data( PUCHAR pData, u16 size, tkip_t *p );
void Mds_MicGet(  void* Adapter,  void* pRxLayer1,  PUCHAR pKey,  PUCHAR pMic );
void Mds_MicFill(  void* Adapter,  void* pDes,  PUCHAR XmitBufAddress );



