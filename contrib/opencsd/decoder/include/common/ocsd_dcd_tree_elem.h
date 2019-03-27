/*!
 * \file       ocsd_dcd_tree_elem.h
 * \brief      OpenCSD : Decode tree element.
 * 
 * \copyright  Copyright (c) 2015, ARM Limited. All Rights Reserved.
 */

/* 
 * Redistribution and use in source and binary forms, with or without modification, 
 * are permitted provided that the following conditions are met:
 * 
 * 1. Redistributions of source code must retain the above copyright notice, 
 * this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright notice, 
 * this list of conditions and the following disclaimer in the documentation 
 * and/or other materials provided with the distribution. 
 * 
 * 3. Neither the name of the copyright holder nor the names of its contributors 
 * may be used to endorse or promote products derived from this software without 
 * specific prior written permission. 
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS 'AS IS' AND 
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, 
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES 
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; 
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND 
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS 
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */ 

#ifndef ARM_OCSD_DCD_TREE_ELEM_H_INCLUDED
#define ARM_OCSD_DCD_TREE_ELEM_H_INCLUDED

#include "common/ocsd_dcd_mngr_i.h"
#include "common/trc_component.h"

/** @addtogroup dcd_tree
@{*/

/*!  @struct _decoder_elements 
 *   @brief Decode tree element base structure.
 *
 *  Element describes the protocol supported for this element and 
 *  contains pointers to the decoder manager interface and component handle.
 */
typedef struct _decoder_elements 
{
    std::string dcd_name;       //!< Registered name of the decoder
    TraceComponent *dcd_handle; //!< handle to the decoder object
    IDecoderMngr *dcd_mngr;     //!< pointer to the decoder manager interface for the decodcer
    ocsd_trace_protocol_t protocol;//!< protocol type 
    bool created;  /**< decode tree created this element (destroy it on tree destruction) */
} decoder_element;

/*!
 *  @class DecodeTreeElement   
 *  @brief Decode tree element
 *
 *  Decoder tree elements are references to individual decoders in the tree.
 *  These allow iteration of all decoders in the tree to perform common operations.
 *  
 * The DecodeTree contains a list of elements.
 */
class DecodeTreeElement : protected decoder_element
{
public:
    DecodeTreeElement();
    ~DecodeTreeElement() {};

    void SetDecoderElement(const std::string &name, IDecoderMngr *dcdMngr, TraceComponent *pHandle, bool bCreated);
    void DestroyElem();

    const std::string &getDecoderTypeName()  { return dcd_name; };
    IDecoderMngr *getDecoderMngr() { return dcd_mngr; };
    ocsd_trace_protocol_t getProtocol() const { return protocol; };
    TraceComponent *getDecoderHandle() { return dcd_handle; };
};

inline DecodeTreeElement::DecodeTreeElement()
{
    dcd_name = "unknown";
    dcd_mngr = 0;
    dcd_handle = 0;
    protocol = OCSD_PROTOCOL_END;  
    created = false;
}

inline void DecodeTreeElement::SetDecoderElement(const std::string &name, IDecoderMngr *dcdMngr, TraceComponent *pHandle, bool bCreated)
{
    dcd_name = name;
    dcd_mngr = dcdMngr;
    dcd_handle = pHandle;
    protocol = OCSD_PROTOCOL_UNKNOWN;  
    if(dcd_mngr)
        protocol = dcd_mngr->getProtocolType();
    created = bCreated;
}

inline void DecodeTreeElement::DestroyElem()
{
    if(created && (dcd_mngr != 0) && (dcd_handle != 0))
        dcd_mngr->destroyDecoder(dcd_handle);
}

/** @}*/
#endif // ARM_OCSD_DCD_TREE_ELEM_H_INCLUDED

/* End of File ocsd_dcd_tree_elem.h */
