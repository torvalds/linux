/******************************************************************************
 *
 * Copyright 2008, Silicon Image, Inc.  All rights reserved.  
 * No part of this work may be reproduced, modified, distributed, transmitted,
 * transcribed, or translated into any language or computer format, in any form
 * or by any means without written permission of: Silicon Image, Inc., 1060
 * East Arques Avenue, Sunnyvale, California 94085
 * 
 *****************************************************************************/
/**
 * @file infofrm.h
 *
 * This is a description of the file. 
 *
 * $Author: $Vladimir Grekhov
 * $Rev: $
 * $Date: $9-15-09
 *
 *****************************************************************************/

#ifndef _INFOFRM_H
#define _INFOFRM_H

/***** macro definitions *****************************************************/

/***** public type definitions ***********************************************/

/***** public function prototypes ********************************************/

/*****************************************************************************/
/**
 *  The description of the function HdmiInitIf(). This function intializes Info Frame 
 *	related registers and data structures. Function should be called only once		
 *
 *  @param[in,out]      none
 *
 *  @return             none
 *  @retval             void
 *
 *****************************************************************************/

void HdmiInitIf ( void );

/*****************************************************************************/
/**
 *  The description of the function HdmiProcIfTo(). This function handles info frame 
 *	time outs		
 *
 *  @param[in] wToStep  Info Frame elapsed from previous to call ( measured in MS)
 *
 *  @return             none
 *  @retval             void
 *
 *****************************************************************************/

void HdmiProcIfTo ( uint16_t wToStep  );

/*****************************************************************************/
/**
 *  The description of the function InterInfoFrmProc(). Processing info frame interrupts
 *
 *
 *  @param[in] bNewInfoFrm  new info frames
 *  @param[in] bNoAvi		no Avi info frames
 *  @param[in] bNoInfoFrm	no info frames
 *
 *  @return             none
 *  @retval             void
 *
 *****************************************************************************/

void InterInfoFrmProc ( uint8_t bNewInfoFrm,
					/*	uint8_t bNoAvi,*/
						uint8_t bNoInfoFrm );

#else /* _INFOFRM_H */


#endif /* _INFOFRM_H */