
#include <linux/types.h>
#include <linux/semaphore.h>
#include "wilc_memory.h"
#include "wilc_strutils.h"


#define tHANDLE	void *

typedef struct {
	u8		*pu8Buffer;
	u32 u32BufferLength;
	u32 u32WriteOffset;
	u32 u32ReadOffset;
	u32 u32TotalBytes;
	struct semaphore SemBuffer;
} tstrFifoHandler;


extern u32 FIFO_InitBuffer(tHANDLE *hBuffer,
								   u32 u32BufferLength);
extern u32 FIFO_DeInit(tHANDLE hFifo);
extern u32 FIFO_ReadBytes(tHANDLE hFifo, u8 *pu8Buffer,
				u32 u32BytesToRead, u32 *pu32BytesRead);
extern u32 FIFO_WriteBytes(tHANDLE hFifo, u8 *pu8Buffer,
				u32 u32BytesToWrite, bool bForceOverWrite);
