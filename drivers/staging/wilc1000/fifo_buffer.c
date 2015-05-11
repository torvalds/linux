

#include "wilc_oswrapper.h"
#include "fifo_buffer.h"



WILC_Uint32 FIFO_InitBuffer(tHANDLE *hBuffer, WILC_Uint32 u32BufferLength)
{
	WILC_Uint32 u32Error = 0;
	tstrFifoHandler *pstrFifoHandler = WILC_MALLOC (sizeof (tstrFifoHandler));
	if (pstrFifoHandler) {
		WILC_memset (pstrFifoHandler, 0, sizeof (tstrFifoHandler));
		pstrFifoHandler->pu8Buffer = WILC_MALLOC (u32BufferLength);
		if (pstrFifoHandler->pu8Buffer)	{
			tstrWILC_SemaphoreAttrs strSemBufferAttrs;
			pstrFifoHandler->u32BufferLength = u32BufferLength;
			WILC_memset (pstrFifoHandler->pu8Buffer, 0, u32BufferLength);
			/* create semaphore */
			WILC_SemaphoreFillDefault (&strSemBufferAttrs);
			strSemBufferAttrs.u32InitCount = 1;
			WILC_SemaphoreCreate(&pstrFifoHandler->SemBuffer, &strSemBufferAttrs);
			*hBuffer = pstrFifoHandler;
		} else {
			*hBuffer = NULL;
			u32Error = 1;
		}
	} else {
		u32Error = 1;
	}
	return u32Error;
}
WILC_Uint32 FIFO_DeInit(tHANDLE hFifo)
{
	WILC_Uint32 u32Error = 0;
	tstrFifoHandler *pstrFifoHandler = (tstrFifoHandler *) hFifo;
	if (pstrFifoHandler) {
		if (pstrFifoHandler->pu8Buffer)	{
			WILC_FREE (pstrFifoHandler->pu8Buffer);
		} else {
			u32Error = 1;
		}

		WILC_SemaphoreDestroy (&pstrFifoHandler->SemBuffer, WILC_NULL);

		WILC_FREE (pstrFifoHandler);
	} else {
		u32Error = 1;
	}
	return u32Error;
}

WILC_Uint32 FIFO_ReadBytes(tHANDLE hFifo, WILC_Uint8 *pu8Buffer, WILC_Uint32 u32BytesToRead, WILC_Uint32 *pu32BytesRead)
{
	WILC_Uint32 u32Error = 0;
	tstrFifoHandler *pstrFifoHandler = (tstrFifoHandler *) hFifo;
	if (pstrFifoHandler && pu32BytesRead) {
		if (pstrFifoHandler->u32TotalBytes) {
			if (WILC_SemaphoreAcquire(&pstrFifoHandler->SemBuffer, WILC_NULL) == WILC_SUCCESS) {
				if (u32BytesToRead > pstrFifoHandler->u32TotalBytes) {
					*pu32BytesRead = pstrFifoHandler->u32TotalBytes;
				} else {
					*pu32BytesRead = u32BytesToRead;
				}
				if ((pstrFifoHandler->u32ReadOffset + u32BytesToRead) <= pstrFifoHandler->u32BufferLength) {
					WILC_memcpy(pu8Buffer, pstrFifoHandler->pu8Buffer + pstrFifoHandler->u32ReadOffset,
						    *pu32BytesRead);
					/* update read offset and total bytes */
					pstrFifoHandler->u32ReadOffset += u32BytesToRead;
					pstrFifoHandler->u32TotalBytes -= u32BytesToRead;

				} else {
					WILC_Uint32 u32FirstPart =
						pstrFifoHandler->u32BufferLength - pstrFifoHandler->u32ReadOffset;
					WILC_memcpy(pu8Buffer, pstrFifoHandler->pu8Buffer + pstrFifoHandler->u32ReadOffset,
						    u32FirstPart);
					WILC_memcpy(pu8Buffer + u32FirstPart, pstrFifoHandler->pu8Buffer,
						    u32BytesToRead - u32FirstPart);
					/* update read offset and total bytes */
					pstrFifoHandler->u32ReadOffset = u32BytesToRead - u32FirstPart;
					pstrFifoHandler->u32TotalBytes -= u32BytesToRead;
				}
				WILC_SemaphoreRelease (&pstrFifoHandler->SemBuffer, WILC_NULL);
			} else {
				u32Error = 1;
			}
		} else {
			u32Error = 1;
		}
	} else {
		u32Error = 1;
	}
	return u32Error;
}

WILC_Uint32 FIFO_WriteBytes(tHANDLE hFifo, WILC_Uint8 *pu8Buffer, WILC_Uint32 u32BytesToWrite, WILC_Bool bForceOverWrite)
{
	WILC_Uint32 u32Error = 0;
	tstrFifoHandler *pstrFifoHandler = (tstrFifoHandler *) hFifo;
	if (pstrFifoHandler) {
		if (u32BytesToWrite < pstrFifoHandler->u32BufferLength)	{
			if ((pstrFifoHandler->u32TotalBytes + u32BytesToWrite) <= pstrFifoHandler->u32BufferLength ||
			    bForceOverWrite) {
				if (WILC_SemaphoreAcquire(&pstrFifoHandler->SemBuffer, WILC_NULL) == WILC_SUCCESS) {
					if ((pstrFifoHandler->u32WriteOffset + u32BytesToWrite) <= pstrFifoHandler->u32BufferLength) {
						WILC_memcpy(pstrFifoHandler->pu8Buffer + pstrFifoHandler->u32WriteOffset, pu8Buffer,
							    u32BytesToWrite);
						/* update read offset and total bytes */
						pstrFifoHandler->u32WriteOffset += u32BytesToWrite;
						pstrFifoHandler->u32TotalBytes  += u32BytesToWrite;

					} else {
						WILC_Uint32 u32FirstPart =
							pstrFifoHandler->u32BufferLength - pstrFifoHandler->u32WriteOffset;
						WILC_memcpy(pstrFifoHandler->pu8Buffer + pstrFifoHandler->u32WriteOffset, pu8Buffer,
							    u32FirstPart);
						WILC_memcpy(pstrFifoHandler->pu8Buffer, pu8Buffer + u32FirstPart,
							    u32BytesToWrite - u32FirstPart);
						/* update read offset and total bytes */
						pstrFifoHandler->u32WriteOffset = u32BytesToWrite - u32FirstPart;
						pstrFifoHandler->u32TotalBytes += u32BytesToWrite;
					}
					/* if data overwriten */
					if (pstrFifoHandler->u32TotalBytes > pstrFifoHandler->u32BufferLength) {
						/* adjust read offset to the oldest data available */
						pstrFifoHandler->u32ReadOffset = pstrFifoHandler->u32WriteOffset;
						/* data availabe is the buffer length */
						pstrFifoHandler->u32TotalBytes = pstrFifoHandler->u32BufferLength;
					}
					WILC_SemaphoreRelease(&pstrFifoHandler->SemBuffer, WILC_NULL);
				}
			} else {
				u32Error = 1;
			}
		} else {
			u32Error = 1;
		}
	} else {
		u32Error = 1;
	}
	return u32Error;
}