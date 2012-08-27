/// **************************************************************************
/// (c) Beceem Communications Inc.
///     All Rights Reserved
///
///   \file        : CmHost.h
///   \author      : Rajeev Tirumala
///   \date        : September 8 , 2006
///   \brief       : Definitions for Connection Management Requests structure
///                  which we will use to setup our connection structures.Its high
///                  time we had a header file for CmHost.cpp to isolate the way
///                  f/w sends DSx messages and the way we interpret them in code.
///                              Revision History
///
///    Date       Author   Version   Description
///  08-Sep-06    Rajeev       0.1      Created
/// **************************************************************************
#ifndef _CM_HOST_H
#define _CM_HOST_H

#pragma once
#pragma pack (push,4)

#define  DSX_MESSAGE_EXCHANGE_BUFFER        0xBF60AC84 // This contains the pointer
#define  DSX_MESSAGE_EXCHANGE_BUFFER_SIZE   72000 // 24 K Bytes

/// \brief structure stLocalSFAddRequest
typedef struct stLocalSFAddRequestAlt{
	B_UINT8                         u8Type;
	B_UINT8      u8Direction;

	B_UINT16                        u16TID;
   /// \brief 16bitCID
    B_UINT16                        u16CID;
    /// \brief 16bitVCID
    B_UINT16                        u16VCID;


	/// \brief structure ParameterSet
    stServiceFlowParamSI              sfParameterSet;

    //USE_MEMORY_MANAGER();
}stLocalSFAddRequestAlt;

/// \brief structure stLocalSFAddIndication
typedef struct stLocalSFAddIndicationAlt{
    B_UINT8                         u8Type;
	B_UINT8      u8Direction;
	B_UINT16                         u16TID;
    /// \brief 16bitCID
    B_UINT16                        u16CID;
    /// \brief 16bitVCID
    B_UINT16                        u16VCID;
	/// \brief structure AuthorizedSet
    stServiceFlowParamSI              sfAuthorizedSet;
    /// \brief structure AdmittedSet
    stServiceFlowParamSI              sfAdmittedSet;
	/// \brief structure ActiveSet
    stServiceFlowParamSI              sfActiveSet;

	B_UINT8 						u8CC;	/**<  Confirmation Code*/
	B_UINT8 						u8Padd; 	/**<  8-bit Padding */
	B_UINT16						u16Padd;	/**< 16 bit Padding */
//    USE_MEMORY_MANAGER();
}stLocalSFAddIndicationAlt;

/// \brief structure stLocalSFAddConfirmation
typedef struct stLocalSFAddConfirmationAlt{
	B_UINT8                     u8Type;
	B_UINT8      				u8Direction;
	B_UINT16					u16TID;
    /// \brief 16bitCID
    B_UINT16                        u16CID;
    /// \brief 16bitVCID
    B_UINT16                        u16VCID;
    /// \brief structure AuthorizedSet
    stServiceFlowParamSI              sfAuthorizedSet;
    /// \brief structure AdmittedSet
    stServiceFlowParamSI              sfAdmittedSet;
    /// \brief structure ActiveSet
    stServiceFlowParamSI              sfActiveSet;
}stLocalSFAddConfirmationAlt;


/// \brief structure stLocalSFChangeRequest
typedef struct stLocalSFChangeRequestAlt{
    B_UINT8                         u8Type;
	B_UINT8      u8Direction;
	B_UINT16					u16TID;
    /// \brief 16bitCID
    B_UINT16                        u16CID;
    /// \brief 16bitVCID
    B_UINT16                        u16VCID;
	/*
	//Pointer location at which following Service Flow param Structure can be read
	//from the target. We get only the address location and we need to read out the
	//entire SF param structure at the given location on target
	*/
    /// \brief structure AuthorizedSet
    stServiceFlowParamSI              sfAuthorizedSet;
    /// \brief structure AdmittedSet
    stServiceFlowParamSI              sfAdmittedSet;
    /// \brief structure ParameterSet
    stServiceFlowParamSI              sfActiveSet;

	B_UINT8 						u8CC;	/**<  Confirmation Code*/
	B_UINT8 						u8Padd; 	/**<  8-bit Padding */
	B_UINT16						u16Padd;	/**< 16 bit */

}stLocalSFChangeRequestAlt;

/// \brief structure stLocalSFChangeConfirmation
typedef struct stLocalSFChangeConfirmationAlt{
	B_UINT8                         u8Type;
	B_UINT8      					u8Direction;
	B_UINT16						u16TID;
    /// \brief 16bitCID
    B_UINT16                        u16CID;
    /// \brief 16bitVCID
    B_UINT16                        u16VCID;
    /// \brief structure AuthorizedSet
    stServiceFlowParamSI              sfAuthorizedSet;
    /// \brief structure AdmittedSet
    stServiceFlowParamSI              sfAdmittedSet;
    /// \brief structure ActiveSet
    stServiceFlowParamSI              sfActiveSet;

}stLocalSFChangeConfirmationAlt;

/// \brief structure stLocalSFChangeIndication
typedef struct stLocalSFChangeIndicationAlt{
	B_UINT8                         u8Type;
		B_UINT8      u8Direction;
	B_UINT16						u16TID;
    /// \brief 16bitCID
    B_UINT16                        u16CID;
    /// \brief 16bitVCID
    B_UINT16                        u16VCID;
    /// \brief structure AuthorizedSet
    stServiceFlowParamSI              sfAuthorizedSet;
    /// \brief structure AdmittedSet
    stServiceFlowParamSI              sfAdmittedSet;
    /// \brief structure ActiveSet
    stServiceFlowParamSI              sfActiveSet;

	B_UINT8 						u8CC;	/**<  Confirmation Code*/
	B_UINT8 						u8Padd; 	/**<  8-bit Padding */
	B_UINT16						u16Padd;	/**< 16 bit */

}stLocalSFChangeIndicationAlt;

ULONG StoreCmControlResponseMessage(struct bcm_mini_adapter *Adapter, PVOID pvBuffer,UINT *puBufferLength);

INT AllocAdapterDsxBuffer(struct bcm_mini_adapter *Adapter);

INT FreeAdapterDsxBuffer(struct bcm_mini_adapter *Adapter);
ULONG SetUpTargetDsxBuffers(struct bcm_mini_adapter *Adapter);

BOOLEAN CmControlResponseMessage(struct bcm_mini_adapter *Adapter, PVOID pvBuffer);


#pragma pack (pop)

#endif
