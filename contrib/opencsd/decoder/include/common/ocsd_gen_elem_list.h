/*
 * \file       ocsd_gen_elem_stack.h
 * \brief      OpenCSD : Generic element output stack.
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

#include <list>
#include "trc_gen_elem.h"
#include "comp_attach_pt_t.h"
#include "interfaces/trc_gen_elem_in_i.h"

/*!
 * @class OcsdGenElemList
 * @brief Maintain a list of elements to be output
 * 
 * Each incoming packet can result in multiple output elements.
 * These are stacked in this class prior to entering the output phase of processing.
 *
 * This should remove some of the requirement on the packet processing to be re-enterant,
 * simplifying this code.
 *
 * Last element(s) on this stack can be marked pending to allow for later cancellation.
 * (This required for cancel element in ETMv3 exeception branch).
 * 
 * The "list" is actually a ring buffer - maintaining pointers to indicate current valid elements.
 * This buffer can increase on demand, but will only be released at the end of a decode session.
 */
class OcsdGenElemList
{
public:
    OcsdGenElemList();
    ~OcsdGenElemList();

    void initSendIf(componentAttachPt<ITrcGenElemIn> *pGenElemIf);
    void initCSID(const uint8_t CSID) { m_CSID = CSID; };

    void reset();   //!< reset the element list.

    OcsdTraceElement *getNextElem(const ocsd_trc_index_t trc_pkt_idx); //!< get next free element on the stack (add one to the output)
    const int getNumElem() const;                                      //!< return the total number of elements on the stack (inlcuding any pended ones).
    
    const ocsd_gen_trc_elem_t getElemType(const int entryN) const;    //!< get the type for the nth element in the stack (0 indexed)

    void pendLastNElem(int numPend);    //!< Last element to be pended prior to cancel/commit decision.
    void commitAllPendElem();           //!< commit all pended elements.
    void cancelPendElem();              //!< cancel the last pended element on the stack.
    const int numPendElem() const;      //!< return the number of pended elements.

    /*! Send all of the none pended elements
        Stop sending when all sent or _CONT response.
     */
    ocsd_datapath_resp_t sendElements(); 
    const bool elemToSend() const;  //!< true if any none-pending elements left to send.

private:

    void growArray();
    const int getAdjustedIdx(int idxIn) const;  //!< get adjusted index into circular buffer.


    // list element contains pointer and byte index in trace stream
    typedef struct _elemPtr {
        OcsdTraceElement *pElem;        //!< pointer to the listed trace element
        ocsd_trc_index_t trc_pkt_idx;   //!< packet index in the trace stream
    } elemPtr_t;

    elemPtr_t *m_pElemArray;    //!< an array of pointers to elements.
    int m_elemArraySize;        //!< number of element pointers in the array 

    int m_firstElemIdx;        //!< internal index in array of first element in use.
    int m_numUsed;             //!< number of elements in use
    int m_numPend;             //!< internal count of pended elements.

    uint8_t m_CSID;

    componentAttachPt<ITrcGenElemIn> *m_sendIf; //!< element send interface.
};

inline const int OcsdGenElemList::getAdjustedIdx(int idxIn) const
{
    if(idxIn >= m_elemArraySize)
        idxIn -= m_elemArraySize;
    return idxIn;
}

inline const int OcsdGenElemList::getNumElem() const 
{ 
    return m_numUsed; 
}

inline const int OcsdGenElemList::numPendElem() const
{
    return m_numPend;
}

inline void OcsdGenElemList::pendLastNElem(int numPend)
{
    if(numPend >= getNumElem())
        m_numPend = numPend;
}

inline void OcsdGenElemList::commitAllPendElem()
{
     m_numPend = 0;
}

inline void OcsdGenElemList::cancelPendElem()
{
    if(m_numPend > 0)
    {
        m_numUsed -= m_numPend;
    }
}

inline const bool OcsdGenElemList::elemToSend() const
{
    return ((getNumElem() - m_numPend) > 0);
}

inline void OcsdGenElemList::initSendIf(componentAttachPt<ITrcGenElemIn> *pGenElemIf)
{
    m_sendIf = pGenElemIf;
}

/* End of File ocsd_gen_elem_stack.h */
