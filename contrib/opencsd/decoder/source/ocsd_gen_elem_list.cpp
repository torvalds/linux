/*
 * \file       ocsd_gen_elem_list.cpp
 * \brief      OpenCSD : List of Generic trace elements for output.
 * 
 * \copyright  Copyright (c) 2016, ARM Limited. All Rights Reserved.
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

#include "common/ocsd_gen_elem_list.h"

OcsdGenElemList::OcsdGenElemList()
{
    m_firstElemIdx=0;        
    m_numUsed=0;         
    m_numPend=0; 

    m_elemArraySize = 0;
    m_sendIf = 0;
    m_CSID = 0;
    m_pElemArray = 0;
}

OcsdGenElemList::~OcsdGenElemList()
{
    for(int i = 0; i<m_elemArraySize; i++)
    {
        delete m_pElemArray[i].pElem;
    }
    delete [] m_pElemArray;
    m_pElemArray = 0;
}

void OcsdGenElemList::reset()
{
    m_firstElemIdx=0;        
    m_numUsed=0;  
    m_numPend=0; 
}

OcsdTraceElement *OcsdGenElemList::getNextElem(const ocsd_trc_index_t trc_pkt_idx)
{
    OcsdTraceElement *pElem = 0;
    if(getNumElem() == m_elemArraySize) // all in use
        growArray();

    if(m_pElemArray != 0)
    {
        m_numUsed++;
        int idx = getAdjustedIdx(m_firstElemIdx + m_numUsed - 1);
        pElem = m_pElemArray[idx].pElem;
        m_pElemArray[idx].trc_pkt_idx = trc_pkt_idx;
    }
    return pElem;
}
    
const ocsd_gen_trc_elem_t OcsdGenElemList::getElemType(const int entryN) const
{
    ocsd_gen_trc_elem_t elem_type =  OCSD_GEN_TRC_ELEM_UNKNOWN;
    if(entryN < getNumElem())
    {
        int idx = getAdjustedIdx(m_firstElemIdx + entryN);
        elem_type = m_pElemArray[idx].pElem->getType();
    }
    return elem_type;
}

ocsd_datapath_resp_t OcsdGenElemList::sendElements()
{
    ocsd_datapath_resp_t resp = OCSD_RESP_CONT;

    if((m_elemArraySize == 0) || (m_sendIf == 0))
        return OCSD_RESP_FATAL_NOT_INIT;

    if(!m_sendIf->hasAttachedAndEnabled())
        return OCSD_RESP_FATAL_NOT_INIT;

    while(elemToSend() && OCSD_DATA_RESP_IS_CONT(resp))
    {
        resp = m_sendIf->first()->TraceElemIn(m_pElemArray[m_firstElemIdx].trc_pkt_idx, m_CSID, *(m_pElemArray[m_firstElemIdx].pElem));
        m_firstElemIdx++;
        if(m_firstElemIdx >= m_elemArraySize)
            m_firstElemIdx = 0;
        m_numUsed--;
    }
    return resp;
}

// this function will enlarge the array, and create extra element objects.
// existing objects will be moved to the front of the array
// called if all elements are in use. (sets indexes accordingly)
void OcsdGenElemList::growArray()
{
    elemPtr_t *p_new_array = 0;

    int increment;
    if(m_elemArraySize == 0)
        // starting from scratch...
        increment = 8;
    else
        increment = m_elemArraySize / 2;    // grow by 50%

     
    p_new_array = new (std::nothrow) elemPtr_t[m_elemArraySize+increment];
    
    if(p_new_array != 0)
    {
        // fill the last increment elements with new objects
        for(int i=0; i < increment; i++)
        {
            p_new_array[m_elemArraySize+i].pElem = new (std::nothrow) OcsdTraceElement();
        }

        // copy the existing objects from the old array to the start of the new one
        // and adjust the indices.
        if(m_elemArraySize > 0)
        {
            int inIdx = m_firstElemIdx;
            for(int i = 0; i < m_elemArraySize; i++)
            {
                p_new_array[i].pElem = m_pElemArray[inIdx].pElem;
                p_new_array[i].trc_pkt_idx = m_pElemArray[inIdx].trc_pkt_idx;
                inIdx++; 
                if(inIdx >= m_elemArraySize)
                    inIdx = 0;
            }
        }

        // delete the old pointer array.
        delete [] m_pElemArray;
        m_elemArraySize += increment;
    }
    else
         m_elemArraySize = 0;

    // update the internal array pointers to the new array
    if(m_firstElemIdx >= 0)
        m_firstElemIdx = 0;   
    m_pElemArray = p_new_array;
}

/* End of File ocsd_gen_elem_list.cpp */
