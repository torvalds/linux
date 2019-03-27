/*
 * \file       trc_etmv4_stack_elem.h
 * \brief      OpenCSD : 
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
#ifndef ARM_TRC_ETMV4_STACK_ELEM_H_INCLUDED
#define ARM_TRC_ETMV4_STACK_ELEM_H_INCLUDED

#include "opencsd/etmv4/trc_pkt_types_etmv4.h"

#include <deque>
#include <vector>

/* ETMv4 I trace stack elements  
    Speculation requires that we stack certain elements till they are committed or 
    cancelled. (P0 elements + other associated parts.)
*/

typedef enum _p0_elem_t 
{
    P0_UNKNOWN,
    P0_ATOM,
    P0_ADDR,
    P0_CTXT,
    P0_TRC_ON,
    P0_EXCEP,
    P0_EXCEP_RET,
    P0_EVENT,
    P0_TS,
    P0_CC,
    P0_TS_CC,
    P0_OVERFLOW
} p0_elem_t;


/************************************************************/
/***Trace stack element base class - 
    record originating packet type and index in buffer*/ 

class TrcStackElem {
public:
     TrcStackElem(const p0_elem_t p0_type, const bool isP0, const ocsd_etmv4_i_pkt_type root_pkt, const ocsd_trc_index_t root_index);
     virtual ~TrcStackElem() {};

     const p0_elem_t getP0Type() const { return m_P0_type; };
     const ocsd_etmv4_i_pkt_type getRootPkt() const { return m_root_pkt; };
     const ocsd_trc_index_t getRootIndex() const  { return m_root_idx; };
     const bool isP0() const { return m_is_P0; };

private:
     ocsd_etmv4_i_pkt_type m_root_pkt;
     ocsd_trc_index_t m_root_idx;
     p0_elem_t m_P0_type;

protected:
     bool m_is_P0;  // true if genuine P0 - commit / cancellable, false otherwise

};

inline TrcStackElem::TrcStackElem(p0_elem_t p0_type, const bool isP0, ocsd_etmv4_i_pkt_type root_pkt, ocsd_trc_index_t root_index) :
    m_root_pkt(root_pkt),
    m_root_idx(root_index),
    m_P0_type(p0_type),
    m_is_P0(isP0)
{
}

/************************************************************/
/** Address element */

class TrcStackElemAddr : public TrcStackElem
{
protected:
    TrcStackElemAddr(const ocsd_etmv4_i_pkt_type root_pkt, const ocsd_trc_index_t root_index);
    virtual ~TrcStackElemAddr() {};

    friend class EtmV4P0Stack;

public:
    void setAddr(const etmv4_addr_val_t &addr_val) { m_addr_val = addr_val; };
    const etmv4_addr_val_t &getAddr() const { return m_addr_val; };

private:
    etmv4_addr_val_t m_addr_val;
};

inline TrcStackElemAddr::TrcStackElemAddr(const ocsd_etmv4_i_pkt_type root_pkt, const ocsd_trc_index_t root_index) :
    TrcStackElem(P0_ADDR, false, root_pkt,root_index)
{
    m_addr_val.val = 0;
    m_addr_val.isa = 0;
}

/************************************************************/
/** Context element */
    
class TrcStackElemCtxt : public TrcStackElem
{
protected:
    TrcStackElemCtxt(const ocsd_etmv4_i_pkt_type root_pkt, const ocsd_trc_index_t root_index);
    virtual ~TrcStackElemCtxt() {};

    friend class EtmV4P0Stack;

public:
    void setContext(const  etmv4_context_t &ctxt) { m_context = ctxt; };
    const  etmv4_context_t &getContext() const  { return m_context; }; 

private:
     etmv4_context_t m_context;
};

inline TrcStackElemCtxt::TrcStackElemCtxt(const ocsd_etmv4_i_pkt_type root_pkt, const ocsd_trc_index_t root_index) :
    TrcStackElem(P0_CTXT, false, root_pkt,root_index)
{
}

/************************************************************/
/** Exception element */

class TrcStackElemExcept : public TrcStackElem
{
protected:
    TrcStackElemExcept(const ocsd_etmv4_i_pkt_type root_pkt, const ocsd_trc_index_t root_index);
    virtual ~TrcStackElemExcept() {};

    friend class EtmV4P0Stack;

public:
    void setPrevSame(bool bSame) { m_prev_addr_same = bSame; };
    const bool getPrevSame() const { return m_prev_addr_same; };

    void setExcepNum(const uint16_t num) { m_excep_num = num; };
    const uint16_t getExcepNum() const { return m_excep_num; };

private:
    bool m_prev_addr_same;
    uint16_t m_excep_num;
};

inline TrcStackElemExcept::TrcStackElemExcept(const ocsd_etmv4_i_pkt_type root_pkt, const ocsd_trc_index_t root_index) :
    TrcStackElem(P0_EXCEP, true, root_pkt,root_index),
        m_prev_addr_same(false)
{
}

/************************************************************/
/** Atom element */
    
class TrcStackElemAtom : public TrcStackElem
{
protected:
    TrcStackElemAtom(const ocsd_etmv4_i_pkt_type root_pkt, const ocsd_trc_index_t root_index);
    virtual ~TrcStackElemAtom() {};

    friend class EtmV4P0Stack;

public:
    void setAtom(const ocsd_pkt_atom &atom) { m_atom = atom; };

    const ocsd_atm_val commitOldest();
    int cancelNewest(const int nCancel);
    const bool isEmpty() const { return (m_atom.num == 0); };

private:
    ocsd_pkt_atom m_atom;
};

inline TrcStackElemAtom::TrcStackElemAtom(const ocsd_etmv4_i_pkt_type root_pkt, const ocsd_trc_index_t root_index) :
    TrcStackElem(P0_ATOM, true, root_pkt,root_index)
{
    m_atom.num = 0;
}

// commit oldest - get value and remove it from pattern
inline const ocsd_atm_val TrcStackElemAtom::commitOldest()
{
    ocsd_atm_val val = (m_atom.En_bits & 0x1) ? ATOM_E : ATOM_N;
    m_atom.num--;
    m_atom.En_bits >>= 1;
    return val;
}

// cancel newest - just reduce the atom count.
inline int TrcStackElemAtom::cancelNewest(const int nCancel)
{
    int nRemove = (nCancel <= m_atom.num) ? nCancel : m_atom.num;
    m_atom.num -= nRemove;
    return nRemove;
}

/************************************************************/
/** Generic param element */

class TrcStackElemParam : public TrcStackElem
{
protected:
    TrcStackElemParam(const p0_elem_t p0_type, const bool isP0, const ocsd_etmv4_i_pkt_type root_pkt, const ocsd_trc_index_t root_index);
    virtual ~TrcStackElemParam() {};

    friend class EtmV4P0Stack;

public:
    void setParam(const uint32_t param, const int nParamNum) { m_param[(nParamNum & 0x3)] = param; };
    const uint32_t &getParam(const int nParamNum) const { return m_param[(nParamNum & 0x3)]; };

private:
    uint32_t m_param[4];    
};

inline TrcStackElemParam::TrcStackElemParam(const p0_elem_t p0_type, const bool isP0, const ocsd_etmv4_i_pkt_type root_pkt, const ocsd_trc_index_t root_index) :
    TrcStackElem(p0_type, isP0, root_pkt,root_index)
{
}

/************************************************************/
/* P0 element stack that allows push of elements, and deletion of elements when done.
*/
class EtmV4P0Stack
{
public:
    EtmV4P0Stack() {};
    ~EtmV4P0Stack();

    void push_front(TrcStackElem *pElem);
    void pop_back();
    TrcStackElem *back();
    size_t size();

    void delete_all();
    void delete_back();
    void delete_popped();

    // creation functions - create and push if successful.
    TrcStackElemParam *createParamElem(const p0_elem_t p0_type, const bool isP0, const ocsd_etmv4_i_pkt_type root_pkt, const ocsd_trc_index_t root_index, const std::vector<uint32_t> &params);
    TrcStackElemParam *createParamElemNoParam(const p0_elem_t p0_type, const bool isP0, const ocsd_etmv4_i_pkt_type root_pkt, const ocsd_trc_index_t root_index);
    TrcStackElemAtom *createAtomElem (const ocsd_etmv4_i_pkt_type root_pkt, const ocsd_trc_index_t root_index, const ocsd_pkt_atom &atom);
    TrcStackElemExcept *createExceptElem(const ocsd_etmv4_i_pkt_type root_pkt, const ocsd_trc_index_t root_index, const bool bSame, const uint16_t excepNum);
    TrcStackElemCtxt *createContextElem(const ocsd_etmv4_i_pkt_type root_pkt, const ocsd_trc_index_t root_index, const etmv4_context_t &context);
    TrcStackElemAddr *createAddrElem(const ocsd_etmv4_i_pkt_type root_pkt, const ocsd_trc_index_t root_index, const etmv4_addr_val_t &addr_val);

private:
    std::deque<TrcStackElem *> m_P0_stack;  //!< P0 decode element stack
    std::vector<TrcStackElem *> m_popped_elem;  //!< save list of popped but not deleted elements.

};

inline EtmV4P0Stack::~EtmV4P0Stack()
{
    delete_all();
    delete_popped();
}

// put an element on the front of the stack
inline void EtmV4P0Stack::push_front(TrcStackElem *pElem)
{
    m_P0_stack.push_front(pElem);
}

// pop last element pointer off the stack and stash it for later deletion
inline void EtmV4P0Stack::pop_back()
{
    m_popped_elem.push_back(m_P0_stack.back());
    m_P0_stack.pop_back();
}

// pop last element pointer off the stack and delete immediately
inline void EtmV4P0Stack::delete_back()
{
    if (m_P0_stack.size() > 0)
    {
        TrcStackElem* pElem = m_P0_stack.back();
        delete pElem;
        m_P0_stack.pop_back();
    }
}

// get a pointer to the last element on the stack
inline TrcStackElem *EtmV4P0Stack::back()
{
    return m_P0_stack.back();
}

// remove and delete all the elements left on the stack
inline void EtmV4P0Stack::delete_all()
{
    while (m_P0_stack.size() > 0)
        delete_back();
    m_P0_stack.clear();
}

// delete list of popped elements.
inline void EtmV4P0Stack::delete_popped()
{
    while (m_popped_elem.size() > 0)
    {
        delete m_popped_elem.back();
        m_popped_elem.pop_back();
    }
    m_popped_elem.clear();
}

// get current number of elements on the stack
inline size_t EtmV4P0Stack::size()
{
    return m_P0_stack.size();
}

#endif // ARM_TRC_ETMV4_STACK_ELEM_H_INCLUDED

/* End of File trc_etmv4_stack_elem.h */
