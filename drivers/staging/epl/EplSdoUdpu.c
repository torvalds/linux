/****************************************************************************

  (c) SYSTEC electronic GmbH, D-07973 Greiz, August-Bebel-Str. 29
      www.systec-electronic.com

  Project:      openPOWERLINK

  Description:  source file for SDO/UDP-Protocolabstractionlayer module

  License:

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions
    are met:

    1. Redistributions of source code must retain the above copyright
       notice, this list of conditions and the following disclaimer.

    2. Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials provided with the distribution.

    3. Neither the name of SYSTEC electronic GmbH nor the names of its
       contributors may be used to endorse or promote products derived
       from this software without prior written permission. For written
       permission, please contact info@systec-electronic.com.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
    "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
    LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
    FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
    COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
    INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
    BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
    LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
    CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
    LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
    ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
    POSSIBILITY OF SUCH DAMAGE.

    Severability Clause:

        If a provision of this License is or becomes illegal, invalid or
        unenforceable in any jurisdiction, that shall not affect:
        1. the validity or enforceability in that jurisdiction of any other
           provision of this License; or
        2. the validity or enforceability in other jurisdictions of that or
           any other provision of this License.

  -------------------------------------------------------------------------

                $RCSfile: EplSdoUdpu.c,v $

                $Author: D.Krueger $

                $Revision: 1.8 $  $Date: 2008/10/17 15:32:32 $

                $State: Exp $

                Build Environment:
                    GCC V3.4

  -------------------------------------------------------------------------

  Revision History:

  2006/06/26 k.t.:   start of the implementation

****************************************************************************/

#include "user/EplSdoUdpu.h"

#if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_SDO_UDP)) != 0)

#if (TARGET_SYSTEM == _LINUX_) && defined(__KERNEL__)
#include "SocketLinuxKernel.h"
#include <linux/completion.h>
#include <linux/sched.h>
#endif

/***************************************************************************/
/*                                                                         */
/*                                                                         */
/*          G L O B A L   D E F I N I T I O N S                            */
/*                                                                         */
/*                                                                         */
/***************************************************************************/

//---------------------------------------------------------------------------
// const defines
//---------------------------------------------------------------------------

#ifndef EPL_SDO_MAX_CONNECTION_UDP
#define EPL_SDO_MAX_CONNECTION_UDP  5
#endif

//---------------------------------------------------------------------------
// local types
//---------------------------------------------------------------------------

typedef struct {
	unsigned long m_ulIpAddr;	// in network byte order
	unsigned int m_uiPort;	// in network byte order

} tEplSdoUdpCon;

// instance table
typedef struct {
	tEplSdoUdpCon m_aSdoAbsUdpConnection[EPL_SDO_MAX_CONNECTION_UDP];
	tEplSequLayerReceiveCb m_fpSdoAsySeqCb;
	SOCKET m_UdpSocket;

#if (TARGET_SYSTEM == _WIN32_)
	HANDLE m_ThreadHandle;
	LPCRITICAL_SECTION m_pCriticalSection;
	CRITICAL_SECTION m_CriticalSection;

#elif (TARGET_SYSTEM == _LINUX_) && defined(__KERNEL__)
	struct completion m_CompletionUdpThread;
	int m_ThreadHandle;
	int m_iTerminateThread;
#endif

} tEplSdoUdpInstance;

//---------------------------------------------------------------------------
// modul globale vars
//---------------------------------------------------------------------------

static tEplSdoUdpInstance SdoUdpInstance_g;

//---------------------------------------------------------------------------
// local function prototypes
//---------------------------------------------------------------------------

#if (TARGET_SYSTEM == _WIN32_)
static DWORD PUBLIC EplSdoUdpThread(LPVOID lpParameter);

#elif (TARGET_SYSTEM == _LINUX_) && defined(__KERNEL__)
static int EplSdoUdpThread(void *pArg_p);
#endif

/***************************************************************************/
/*                                                                         */
/*                                                                         */
/*          C L A S S  <EPL-SDO-UDP-Layer>                                 */
/*                                                                         */
/*                                                                         */
/***************************************************************************/
//
// Description: Protocolabstraction layer for UDP
//
//
/***************************************************************************/

//=========================================================================//
//                                                                         //
//          P U B L I C   F U N C T I O N S                                //
//                                                                         //
//=========================================================================//

//---------------------------------------------------------------------------
//
// Function:    EplSdoUdpuInit
//
// Description: init first instance of the module
//
//
//
// Parameters:  pReceiveCb_p    =   functionpointer to Sdo-Sequence layer
//                                  callback-function
//
//
// Returns:     tEplKernel  = Errorcode
//
//
// State:
//
//---------------------------------------------------------------------------
tEplKernel PUBLIC EplSdoUdpuInit(tEplSequLayerReceiveCb fpReceiveCb_p)
{
	tEplKernel Ret;

	Ret = EplSdoUdpuAddInstance(fpReceiveCb_p);

	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplSdoUdpuAddInstance
//
// Description: init additional instance of the module
//              înit socket and start Listen-Thread
//
//
//
// Parameters:  pReceiveCb_p    =   functionpointer to Sdo-Sequence layer
//                                  callback-function
//
//
// Returns:     tEplKernel  = Errorcode
//
//
// State:
//
//---------------------------------------------------------------------------
tEplKernel PUBLIC EplSdoUdpuAddInstance(tEplSequLayerReceiveCb fpReceiveCb_p)
{
	tEplKernel Ret;

#if (TARGET_SYSTEM == _WIN32_)
	int iError;
	WSADATA Wsa;

#endif

	// set instance variables to 0
	EPL_MEMSET(&SdoUdpInstance_g, 0x00, sizeof(SdoUdpInstance_g));

	Ret = kEplSuccessful;

	// save pointer to callback-function
	if (fpReceiveCb_p != NULL) {
		SdoUdpInstance_g.m_fpSdoAsySeqCb = fpReceiveCb_p;
	} else {
		Ret = kEplSdoUdpMissCb;
		goto Exit;
	}

#if (TARGET_SYSTEM == _WIN32_)
	// start winsock2 for win32
	// windows specific start of socket
	iError = WSAStartup(MAKEWORD(2, 0), &Wsa);
	if (iError != 0) {
		Ret = kEplSdoUdpNoSocket;
		goto Exit;
	}
	// create critical section for acccess of instnace variables
	SdoUdpInstance_g.m_pCriticalSection =
	    &SdoUdpInstance_g.m_CriticalSection;
	InitializeCriticalSection(SdoUdpInstance_g.m_pCriticalSection);

#elif (TARGET_SYSTEM == _LINUX_) && defined(__KERNEL__)
	init_completion(&SdoUdpInstance_g.m_CompletionUdpThread);
	SdoUdpInstance_g.m_iTerminateThread = 0;
#endif

	SdoUdpInstance_g.m_ThreadHandle = 0;
	SdoUdpInstance_g.m_UdpSocket = INVALID_SOCKET;

	Ret = EplSdoUdpuConfig(INADDR_ANY, 0);

      Exit:
	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplSdoUdpuDelInstance
//
// Description: del instance of the module
//              del socket and del Listen-Thread
//
//
//
// Parameters:
//
//
// Returns:     tEplKernel  = Errorcode
//
//
// State:
//
//---------------------------------------------------------------------------
tEplKernel PUBLIC EplSdoUdpuDelInstance()
{
	tEplKernel Ret;

#if (TARGET_SYSTEM == _WIN32_)
	BOOL fTermError;
#endif

	Ret = kEplSuccessful;

	if (SdoUdpInstance_g.m_ThreadHandle != 0) {	// listen thread was started
		// close thread
#if (TARGET_SYSTEM == _WIN32_)
		fTermError =
		    TerminateThread(SdoUdpInstance_g.m_ThreadHandle, 0);
		if (fTermError == FALSE) {
			Ret = kEplSdoUdpThreadError;
			goto Exit;
		}
#elif (TARGET_SYSTEM == _LINUX_) && defined(__KERNEL__)
		SdoUdpInstance_g.m_iTerminateThread = 1;
		/* kill_proc(SdoUdpInstance_g.m_ThreadHandle, SIGTERM, 1 ); */
		send_sig(SIGTERM, SdoUdpInstance_g.m_ThreadHandle, 1);
		wait_for_completion(&SdoUdpInstance_g.m_CompletionUdpThread);
#endif

		SdoUdpInstance_g.m_ThreadHandle = 0;
	}

	if (SdoUdpInstance_g.m_UdpSocket != INVALID_SOCKET) {
		// close socket
		closesocket(SdoUdpInstance_g.m_UdpSocket);
		SdoUdpInstance_g.m_UdpSocket = INVALID_SOCKET;
	}
#if (TARGET_SYSTEM == _WIN32_)
	// delete critical section
	DeleteCriticalSection(SdoUdpInstance_g.m_pCriticalSection);
#endif

#if (TARGET_SYSTEM == _WIN32_)
	// for win 32
	WSACleanup();
#endif

#if (TARGET_SYSTEM == _WIN32_)
      Exit:
#endif
	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplSdoUdpuConfig
//
// Description: reconfigurate socket with new IP-Address
//              -> needed for NMT ResetConfiguration
//
// Parameters:  ulIpAddr_p      = IpAddress in platform byte order
//              uiPort_p        = port number in platform byte order
//
//
// Returns:     tEplKernel  = Errorcode
//
//
// State:
//
//---------------------------------------------------------------------------
tEplKernel PUBLIC EplSdoUdpuConfig(unsigned long ulIpAddr_p,
				   unsigned int uiPort_p)
{
	tEplKernel Ret;
	struct sockaddr_in Addr;
	int iError;

#if (TARGET_SYSTEM == _WIN32_)
	BOOL fTermError;
	unsigned long ulThreadId;
#endif

	Ret = kEplSuccessful;

	if (uiPort_p == 0) {	// set UDP port to default port number
		uiPort_p = EPL_C_SDO_EPL_PORT;
	} else if (uiPort_p > 65535) {
		Ret = kEplSdoUdpSocketError;
		goto Exit;
	}

	if (SdoUdpInstance_g.m_ThreadHandle != 0) {	// listen thread was started

		// close old thread
#if (TARGET_SYSTEM == _WIN32_)
		fTermError =
		    TerminateThread(SdoUdpInstance_g.m_ThreadHandle, 0);
		if (fTermError == FALSE) {
			Ret = kEplSdoUdpThreadError;
			goto Exit;
		}
#elif (TARGET_SYSTEM == _LINUX_) && defined(__KERNEL__)
		SdoUdpInstance_g.m_iTerminateThread = 1;
		/* kill_proc(SdoUdpInstance_g.m_ThreadHandle, SIGTERM, 1 ); */
		send_sig(SIGTERM, SdoUdpInstance_g.m_ThreadHandle, 1);
		wait_for_completion(&SdoUdpInstance_g.m_CompletionUdpThread);
		SdoUdpInstance_g.m_iTerminateThread = 0;
#endif

		SdoUdpInstance_g.m_ThreadHandle = 0;
	}

	if (SdoUdpInstance_g.m_UdpSocket != INVALID_SOCKET) {
		// close socket
		iError = closesocket(SdoUdpInstance_g.m_UdpSocket);
		SdoUdpInstance_g.m_UdpSocket = INVALID_SOCKET;
		if (iError != 0) {
			Ret = kEplSdoUdpSocketError;
			goto Exit;
		}
	}
	// create Socket
	SdoUdpInstance_g.m_UdpSocket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (SdoUdpInstance_g.m_UdpSocket == INVALID_SOCKET) {
		Ret = kEplSdoUdpNoSocket;
		EPL_DBGLVL_SDO_TRACE0("EplSdoUdpuConfig: socket() failed\n");
		goto Exit;
	}
	// bind socket
	Addr.sin_family = AF_INET;
	Addr.sin_port = htons((unsigned short)uiPort_p);
	Addr.sin_addr.s_addr = htonl(ulIpAddr_p);
	iError =
	    bind(SdoUdpInstance_g.m_UdpSocket, (struct sockaddr *)&Addr,
		 sizeof(Addr));
	if (iError < 0) {
		//iError = WSAGetLastError();
		EPL_DBGLVL_SDO_TRACE1
		    ("EplSdoUdpuConfig: bind() finished with %i\n", iError);
		Ret = kEplSdoUdpNoSocket;
		goto Exit;
	}
	// create Listen-Thread
#if (TARGET_SYSTEM == _WIN32_)
	// for win32

	// create thread
	SdoUdpInstance_g.m_ThreadHandle = CreateThread(NULL,
						       0,
						       EplSdoUdpThread,
						       &SdoUdpInstance_g,
						       0, &ulThreadId);
	if (SdoUdpInstance_g.m_ThreadHandle == NULL) {
		Ret = kEplSdoUdpThreadError;
		goto Exit;
	}
#elif (TARGET_SYSTEM == _LINUX_) && defined(__KERNEL__)

	SdoUdpInstance_g.m_ThreadHandle =
	    kernel_thread(EplSdoUdpThread, &SdoUdpInstance_g, CLONE_KERNEL);
	if (SdoUdpInstance_g.m_ThreadHandle == 0) {
		Ret = kEplSdoUdpThreadError;
		goto Exit;
	}
#endif

      Exit:
	return Ret;

}

//---------------------------------------------------------------------------
//
// Function:    EplSdoUdpuInitCon
//
// Description: init a new connect
//
//
//
// Parameters:  pSdoConHandle_p = pointer for the new connection handle
//              uiTargetNodeId_p = NodeId of the target node
//
//
// Returns:     tEplKernel  = Errorcode
//
//
// State:
//
//---------------------------------------------------------------------------
tEplKernel PUBLIC EplSdoUdpuInitCon(tEplSdoConHdl * pSdoConHandle_p,
				    unsigned int uiTargetNodeId_p)
{
	tEplKernel Ret;
	unsigned int uiCount;
	unsigned int uiFreeCon;
	tEplSdoUdpCon *pSdoUdpCon;

	Ret = kEplSuccessful;

	// get free entry in control structure
	uiCount = 0;
	uiFreeCon = EPL_SDO_MAX_CONNECTION_UDP;
	pSdoUdpCon = &SdoUdpInstance_g.m_aSdoAbsUdpConnection[0];
	while (uiCount < EPL_SDO_MAX_CONNECTION_UDP) {
		if ((pSdoUdpCon->m_ulIpAddr & htonl(0xFF)) == htonl(uiTargetNodeId_p)) {	// existing connection to target node found
			// set handle
			*pSdoConHandle_p = (uiCount | EPL_SDO_UDP_HANDLE);

			goto Exit;
		} else if ((pSdoUdpCon->m_ulIpAddr == 0)
			   && (pSdoUdpCon->m_uiPort == 0)) {
			uiFreeCon = uiCount;
		}
		uiCount++;
		pSdoUdpCon++;
	}

	if (uiFreeCon == EPL_SDO_MAX_CONNECTION_UDP) {
		// error no free handle
		Ret = kEplSdoUdpNoFreeHandle;
	} else {
		pSdoUdpCon =
		    &SdoUdpInstance_g.m_aSdoAbsUdpConnection[uiFreeCon];
		// save infos for connection
		pSdoUdpCon->m_uiPort = htons(EPL_C_SDO_EPL_PORT);
		pSdoUdpCon->m_ulIpAddr = htonl(0xC0A86400 | uiTargetNodeId_p);	// 192.168.100.uiTargetNodeId_p

		// set handle
		*pSdoConHandle_p = (uiFreeCon | EPL_SDO_UDP_HANDLE);

	}

      Exit:
	return Ret;

}

//---------------------------------------------------------------------------
//
// Function:    EplSdoUdpuSendData
//
// Description: send data using exisiting connection
//
//
//
// Parameters:  SdoConHandle_p  = connection handle
//              pSrcData_p      = pointer to data
//              dwDataSize_p    = number of databyte
//                                  -> without asend-header!!!
//
// Returns:     tEplKernel  = Errorcode
//
//
// State:
//
//---------------------------------------------------------------------------
tEplKernel PUBLIC EplSdoUdpuSendData(tEplSdoConHdl SdoConHandle_p,
				     tEplFrame * pSrcData_p, DWORD dwDataSize_p)
{
	tEplKernel Ret;
	int iError;
	unsigned int uiArray;
	struct sockaddr_in Addr;

	Ret = kEplSuccessful;

	uiArray = (SdoConHandle_p & ~EPL_SDO_ASY_HANDLE_MASK);
	if (uiArray >= EPL_SDO_MAX_CONNECTION_UDP) {
		Ret = kEplSdoUdpInvalidHdl;
		goto Exit;
	}
	//set message type
	AmiSetByteToLe(&pSrcData_p->m_le_bMessageType, 0x06);	// SDO
	// target node id (for Udp = 0)
	AmiSetByteToLe(&pSrcData_p->m_le_bDstNodeId, 0x00);
	// set source-nodeid (for Udp = 0)
	AmiSetByteToLe(&pSrcData_p->m_le_bSrcNodeId, 0x00);

	// calc size
	dwDataSize_p += EPL_ASND_HEADER_SIZE;

	// call sendto
	Addr.sin_family = AF_INET;
#if (TARGET_SYSTEM == _WIN32_)
	// enter  critical section for process function
	EnterCriticalSection(SdoUdpInstance_g.m_pCriticalSection);
#endif

	Addr.sin_port =
	    (unsigned short)SdoUdpInstance_g.m_aSdoAbsUdpConnection[uiArray].
	    m_uiPort;
	Addr.sin_addr.s_addr =
	    SdoUdpInstance_g.m_aSdoAbsUdpConnection[uiArray].m_ulIpAddr;

#if (TARGET_SYSTEM == _WIN32_)
	// leave critical section for process function
	LeaveCriticalSection(SdoUdpInstance_g.m_pCriticalSection);
#endif

	iError = sendto(SdoUdpInstance_g.m_UdpSocket,	// sockethandle
			(const char *)&pSrcData_p->m_le_bMessageType,	// data to send
			dwDataSize_p,	// number of bytes to send
			0,	// flags
			(struct sockaddr *)&Addr,	// target
			sizeof(struct sockaddr_in));	// sizeof targetadress
	if (iError < 0) {
		EPL_DBGLVL_SDO_TRACE1
		    ("EplSdoUdpuSendData: sendto() finished with %i\n", iError);
		Ret = kEplSdoUdpSendError;
		goto Exit;
	}

      Exit:
	return Ret;

}

//---------------------------------------------------------------------------
//
// Function:    EplSdoUdpuDelCon
//
// Description: delete connection from intern structure
//
//
//
// Parameters:  SdoConHandle_p  = connection handle
//
// Returns:     tEplKernel  = Errorcode
//
//
// State:
//
//---------------------------------------------------------------------------
tEplKernel PUBLIC EplSdoUdpuDelCon(tEplSdoConHdl SdoConHandle_p)
{
	tEplKernel Ret;
	unsigned int uiArray;

	uiArray = (SdoConHandle_p & ~EPL_SDO_ASY_HANDLE_MASK);

	if (uiArray >= EPL_SDO_MAX_CONNECTION_UDP) {
		Ret = kEplSdoUdpInvalidHdl;
		goto Exit;
	} else {
		Ret = kEplSuccessful;
	}

	// delete connection
	SdoUdpInstance_g.m_aSdoAbsUdpConnection[uiArray].m_ulIpAddr = 0;
	SdoUdpInstance_g.m_aSdoAbsUdpConnection[uiArray].m_uiPort = 0;

      Exit:
	return Ret;
}

//=========================================================================//
//                                                                         //
//          P R I V A T E   F U N C T I O N S                              //
//                                                                         //
//=========================================================================//

//---------------------------------------------------------------------------
//
// Function:        EplSdoUdpThread
//
// Description:     thread check socket for new data
//
//
//
// Parameters:      lpParameter = pointer to parameter type tEplSdoUdpThreadPara
//
//
// Returns:         DWORD   =   errorcode
//
//
// State:
//
//---------------------------------------------------------------------------
#if (TARGET_SYSTEM == _WIN32_)
static DWORD PUBLIC EplSdoUdpThread(LPVOID lpParameter)
#elif (TARGET_SYSTEM == _LINUX_) && defined(__KERNEL__)
static int EplSdoUdpThread(void *pArg_p)
#endif
{

	tEplSdoUdpInstance *pInstance;
	struct sockaddr_in RemoteAddr;
	int iError;
	int iCount;
	int iFreeEntry;
	BYTE abBuffer[EPL_MAX_SDO_REC_FRAME_SIZE];
	unsigned int uiSize;
	tEplSdoConHdl SdoConHdl;

#if (TARGET_SYSTEM == _WIN32_)
	pInstance = (tEplSdoUdpInstance *) lpParameter;

	for (;;)
#elif (TARGET_SYSTEM == _LINUX_) && defined(__KERNEL__)
	pInstance = (tEplSdoUdpInstance *) pArg_p;
	daemonize("EplSdoUdpThread");
	allow_signal(SIGTERM);

	for (; pInstance->m_iTerminateThread == 0;)
#endif

	{
		// wait for data
		uiSize = sizeof(struct sockaddr);
		iError = recvfrom(pInstance->m_UdpSocket,	// Socket
				  (char *)&abBuffer[0],	// buffer for data
				  sizeof(abBuffer),	// size of the buffer
				  0,	// flags
				  (struct sockaddr *)&RemoteAddr,
				  (int *)&uiSize);
#if (TARGET_SYSTEM == _LINUX_) && defined(__KERNEL__)
		if (iError == -ERESTARTSYS) {
			break;
		}
#endif
		if (iError > 0) {
			// get handle for higher layer
			iCount = 0;
			iFreeEntry = 0xFFFF;
#if (TARGET_SYSTEM == _WIN32_)
			// enter  critical section for process function
			EnterCriticalSection(SdoUdpInstance_g.
					     m_pCriticalSection);
#endif
			while (iCount < EPL_SDO_MAX_CONNECTION_UDP) {
				// check if this connection is already known
				if ((pInstance->m_aSdoAbsUdpConnection[iCount].
				     m_ulIpAddr == RemoteAddr.sin_addr.s_addr)
				    && (pInstance->
					m_aSdoAbsUdpConnection[iCount].
					m_uiPort == RemoteAddr.sin_port)) {
					break;
				}

				if ((pInstance->m_aSdoAbsUdpConnection[iCount].
				     m_ulIpAddr == 0)
				    && (pInstance->
					m_aSdoAbsUdpConnection[iCount].
					m_uiPort == 0)
				    && (iFreeEntry == 0xFFFF))
				{
					iFreeEntry = iCount;
				}

				iCount++;
			}

			if (iCount == EPL_SDO_MAX_CONNECTION_UDP) {
				// connection unknown
				// see if there is a free handle
				if (iFreeEntry != 0xFFFF) {
					// save adress infos
					pInstance->
					    m_aSdoAbsUdpConnection[iFreeEntry].
					    m_ulIpAddr =
					    RemoteAddr.sin_addr.s_addr;
					pInstance->
					    m_aSdoAbsUdpConnection[iFreeEntry].
					    m_uiPort = RemoteAddr.sin_port;
#if (TARGET_SYSTEM == _WIN32_)
					// leave critical section for process function
					LeaveCriticalSection(SdoUdpInstance_g.
							     m_pCriticalSection);
#endif
					// call callback
					SdoConHdl = iFreeEntry;
					SdoConHdl |= EPL_SDO_UDP_HANDLE;
					// offset 4 -> start of SDO Sequence header
					pInstance->m_fpSdoAsySeqCb(SdoConHdl,
								   (tEplAsySdoSeq
								    *) &
								   abBuffer[4],
								   (iError -
								    4));
				} else {
					EPL_DBGLVL_SDO_TRACE0
					    ("Error in EplSdoUdpThread() no free handle\n");
#if (TARGET_SYSTEM == _WIN32_)
					// leave critical section for process function
					LeaveCriticalSection(SdoUdpInstance_g.
							     m_pCriticalSection);
#endif
				}

			} else {
				// known connection
				// call callback with correct handle
				SdoConHdl = iCount;
				SdoConHdl |= EPL_SDO_UDP_HANDLE;
#if (TARGET_SYSTEM == _WIN32_)
				// leave critical section for process function
				LeaveCriticalSection(SdoUdpInstance_g.
						     m_pCriticalSection);
#endif
				// offset 4 -> start of SDO Sequence header
				pInstance->m_fpSdoAsySeqCb(SdoConHdl,
							   (tEplAsySdoSeq *) &
							   abBuffer[4],
							   (iError - 4));
			}
		}		// end of  if(iError!=SOCKET_ERROR)
	}			// end of for(;;)

#if (TARGET_SYSTEM == _LINUX_) && defined(__KERNEL__)
	complete_and_exit(&SdoUdpInstance_g.m_CompletionUdpThread, 0);
#endif

	return 0;
}

#endif // end of #if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_SDO_UDP)) != 0)

// EOF
