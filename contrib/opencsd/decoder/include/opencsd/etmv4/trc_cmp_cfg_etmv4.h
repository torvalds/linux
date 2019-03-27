/*
 * \file       trc_cmp_cfg_etmv4.h
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

#ifndef ARM_TRC_CMP_CFG_ETMV4_H_INCLUDED
#define ARM_TRC_CMP_CFG_ETMV4_H_INCLUDED

#include "trc_pkt_types_etmv4.h"
#include "common/trc_cs_config.h"


/** @addtogroup ocsd_protocol_cfg
@{*/

/** @name ETMv4 configuration
@{*/

/*!
 * @class EtmV4Config   
 * @brief Interpreter class for etm v4 config structure.
 * 
 * Provides quick value interpretation methods for the ETMv4 config register values.
 * Primarily inlined for efficient code. 
 */
class EtmV4Config : public CSConfig // public ocsd_etmv4_cfg
{
public:
    EtmV4Config();  /**< Default constructor */
    EtmV4Config(const ocsd_etmv4_cfg *cfg_regs);  
    ~EtmV4Config() {}; /**< Default destructor */

// operations to convert to and from C-API structure

    //! copy assignment operator for base structure into class.
    EtmV4Config & operator=(const ocsd_etmv4_cfg *p_cfg);

    //! cast operator returning struct const reference
    operator const ocsd_etmv4_cfg &() const { return m_cfg; };
    //! cast operator returning struct const pointer
    operator const ocsd_etmv4_cfg *() const { return &m_cfg; };

    const ocsd_core_profile_t &coreProfile() const { return m_cfg.core_prof; };
    const ocsd_arch_version_t &archVersion() const { return m_cfg.arch_ver; };

    /* idr 0 */
    const bool LSasInstP0() const;
    const bool hasDataTrace() const;
    const bool hasBranchBroadcast() const;
    const bool hasCondTrace() const;
    const bool hasCycleCountI() const;
    const bool hasRetStack() const;
    const uint8_t numEvents() const;
    
    typedef enum _condType {
        COND_PASS_FAIL,
        COND_HAS_ASPR
    } condType;

    const condType hasCondType() const;

    typedef enum _QSuppType {
        Q_NONE,
        Q_ICOUNT_ONLY,
        Q_NO_ICOUNT_ONLY,
        Q_FULL
    } QSuppType;

    const QSuppType getQSuppType();
    const bool hasQElem();
    const bool hasQFilter();

    const bool hasTrcExcpData() const;
    const uint32_t TimeStampSize() const;

    const bool commitOpt1() const;

    /* idr 1 */
    const uint8_t MajVersion() const;
    const uint8_t MinVersion() const;
    
    /* idr 2 */
    const uint32_t iaSizeMax() const;
    const uint32_t cidSize() const;
    const uint32_t vmidSize();
    const uint32_t daSize() const;
    const uint32_t dvSize() const;
    const uint32_t ccSize() const;
    const bool vmidOpt() const;

    /* id regs 8-13*/
    const uint32_t MaxSpecDepth() const;
    const uint32_t P0_Key_Max() const;
    const uint32_t P1_Key_Max() const;
    const uint32_t P1_Spcl_Key_Max() const;
    const uint32_t CondKeyMax() const;
    const uint32_t CondSpecKeyMax() const;
    const uint32_t CondKeyMaxIncr() const;
 
    /* trace idr */
    virtual const uint8_t getTraceID() const; //!< CoreSight Trace ID for this device.

    /* config R */
    const bool enabledDVTrace() const;
    const bool enabledDATrace() const; 
    const bool enabledDataTrace() const;

    typedef enum {
        LSP0_NONE,
        LSP0_L,
        LSP0_S,
        LSP0_LS
    } LSP0_t;

    const bool enabledLSP0Trace() const;
    const LSP0_t LSP0Type() const;
    
    const bool enabledBrBroad() const;
    const bool enabledCCI() const;
    const bool enabledCID() const;
    const bool enabledVMID() const;

    typedef enum {
        COND_TR_DIS,
        COND_TR_LD,
        COND_TR_ST,
        COND_TR_LDST,
        COND_TR_ALL
    } CondITrace_t;

    const CondITrace_t enabledCondITrace();

    const bool enabledTS() const;
    const bool enabledRetStack() const;

    const bool enabledQE() const;

private:
    void PrivateInit();
    void CalcQSupp();   
    void CalcVMIDSize();

    bool m_QSuppCalc;
    bool m_QSuppFilter;
    QSuppType m_QSuppType;

    bool m_VMIDSzCalc;
    uint32_t m_VMIDSize;

    bool m_condTraceCalc;
    CondITrace_t m_CondTrace;

    ocsd_etmv4_cfg m_cfg;
};

/* idr 0 */
inline const bool EtmV4Config::LSasInstP0() const
{
    return (bool)((m_cfg.reg_idr0 & 0x6) == 0x6);
}

inline const bool EtmV4Config::hasDataTrace() const
{
    return (bool)((m_cfg.reg_idr0 & 0x18) == 0x18);
}

inline const bool EtmV4Config::hasBranchBroadcast() const
{
    return (bool)((m_cfg.reg_idr0 & 0x20) == 0x20);
}

inline const bool EtmV4Config::hasCondTrace() const
{
    return (bool)((m_cfg.reg_idr0 & 0x40) == 0x40);
}

inline const bool EtmV4Config::hasCycleCountI() const
{
    return (bool)((m_cfg.reg_idr0 & 0x80) == 0x80);
}

inline const bool EtmV4Config::hasRetStack() const
{
    return (bool)((m_cfg.reg_idr0 & 0x200) == 0x200);
}

inline const uint8_t EtmV4Config::numEvents() const
{
    return ((m_cfg.reg_idr0 >> 10) & 0x3) + 1;
}

inline const EtmV4Config::condType EtmV4Config::hasCondType() const
{
    return ((m_cfg.reg_idr0 & 0x3000) == 0x1000) ? EtmV4Config::COND_HAS_ASPR : EtmV4Config::COND_PASS_FAIL;
}

inline const EtmV4Config::QSuppType EtmV4Config::getQSuppType()
{
    if(!m_QSuppCalc) CalcQSupp();
    return m_QSuppType;
}

inline const bool EtmV4Config::hasQElem()
{
    if(!m_QSuppCalc) CalcQSupp();
    return (bool)(m_QSuppType != Q_NONE);
}

inline const bool EtmV4Config::hasQFilter()
{
    if(!m_QSuppCalc) CalcQSupp();
    return m_QSuppFilter;
}

inline const bool EtmV4Config::hasTrcExcpData() const
{
    return (bool)((m_cfg.reg_idr0 & 0x20000) == 0x20000);
}

inline const uint32_t EtmV4Config::TimeStampSize() const
{
    uint32_t tsSizeF = (m_cfg.reg_idr0 >> 24) & 0x1F;
    if(tsSizeF == 0x6)
        return 48;
    if(tsSizeF == 0x8)
        return 64;
    return 0;
}

inline const bool EtmV4Config::commitOpt1() const
{
    return (bool)((m_cfg.reg_idr0 & 0x20000000) == 0x20000000) && hasCycleCountI();
}

    /* idr 1 */
inline const uint8_t EtmV4Config::MajVersion() const
{
    return (uint8_t)((m_cfg.reg_idr1 >> 8) & 0xF);
}

inline const uint8_t EtmV4Config::MinVersion() const
{
    return (uint8_t)((m_cfg.reg_idr1 >> 4) & 0xF);
}


/* idr 2 */
inline const uint32_t EtmV4Config::iaSizeMax() const
{
    return ((m_cfg.reg_idr2 & 0x1F) == 0x8) ? 64 : 32;
}

inline const uint32_t EtmV4Config::cidSize() const
{
    return (((m_cfg.reg_idr2 >> 5) & 0x1F) == 0x4) ? 32 : 0;
}

inline const uint32_t EtmV4Config::vmidSize()
{
    if(!m_VMIDSzCalc) 
    {
        CalcVMIDSize();
    }
    return m_VMIDSize;
}

inline const uint32_t EtmV4Config::daSize() const
{
    uint32_t daSizeF = ((m_cfg.reg_idr2 >> 15) & 0x1F);
    if(daSizeF)
        return (((m_cfg.reg_idr2 >> 15) & 0x1F) == 0x8) ? 64 : 32;
    return 0;
}

inline const uint32_t EtmV4Config::dvSize() const
{
    uint32_t dvSizeF = ((m_cfg.reg_idr2 >> 20) & 0x1F);
    if(dvSizeF)
        return (((m_cfg.reg_idr2 >> 20) & 0x1F) == 0x8) ? 64 : 32;
    return 0;
}

inline const uint32_t EtmV4Config::ccSize() const
{
    return ((m_cfg.reg_idr2 >> 25) & 0xF) + 12;
}

inline const bool EtmV4Config::vmidOpt() const
{
    return (bool)((m_cfg.reg_idr2 & 0x20000000) == 0x20000000) && (MinVersion() > 0);
}

/* id regs 8-13*/

inline const uint32_t EtmV4Config::MaxSpecDepth() const
{
    return m_cfg.reg_idr8;
}

inline const uint32_t EtmV4Config::P0_Key_Max() const
{
    return (m_cfg.reg_idr9 == 0) ? 1 : m_cfg.reg_idr9;
}

inline const uint32_t EtmV4Config::P1_Key_Max() const
{
    return m_cfg.reg_idr10;
}

inline const uint32_t  EtmV4Config::P1_Spcl_Key_Max() const
{
    return m_cfg.reg_idr11;
}

inline const uint32_t EtmV4Config::CondKeyMax() const
{
    return m_cfg.reg_idr12;
}

inline const uint32_t EtmV4Config::CondSpecKeyMax() const
{
    return m_cfg.reg_idr13;
}

inline const uint32_t EtmV4Config::CondKeyMaxIncr() const
{
    return m_cfg.reg_idr12 - m_cfg.reg_idr13;
}

inline const uint8_t EtmV4Config::getTraceID() const
{
    return (uint8_t)(m_cfg.reg_traceidr & 0x7F);
}

    /* config R */
inline const bool EtmV4Config::enabledDVTrace() const
{
    return hasDataTrace() && enabledLSP0Trace() && ((m_cfg.reg_configr & (0x1 << 17)) != 0);
}

inline const bool EtmV4Config::enabledDATrace() const
{
    return hasDataTrace() && enabledLSP0Trace() && ((m_cfg.reg_configr & (0x1 << 16)) != 0);
}

inline const bool EtmV4Config::enabledDataTrace() const
{
    return enabledDATrace() || enabledDVTrace();
}

inline const bool EtmV4Config::enabledLSP0Trace() const
{
    return ((m_cfg.reg_configr & 0x6) != 0);
}

inline const EtmV4Config::LSP0_t EtmV4Config::LSP0Type() const
{
    return (LSP0_t)((m_cfg.reg_configr & 0x6) >> 1);
}
    
inline const bool EtmV4Config::enabledBrBroad() const
{
    return ((m_cfg.reg_configr & (0x1 << 3)) != 0);
}

inline const bool EtmV4Config::enabledCCI() const
{
    return ((m_cfg.reg_configr & (0x1 << 4)) != 0);
}

inline const bool EtmV4Config::enabledCID() const
{
    return ((m_cfg.reg_configr & (0x1 << 6)) != 0);
}

inline const bool EtmV4Config::enabledVMID() const
{
    return ((m_cfg.reg_configr & (0x1 << 7)) != 0);
}

inline const EtmV4Config::CondITrace_t EtmV4Config::enabledCondITrace()
{
    if(!m_condTraceCalc)
    {
        switch((m_cfg.reg_configr >> 8) & 0x7)
        {
        default:
        case 0: m_CondTrace = COND_TR_DIS; break;
        case 1: m_CondTrace = COND_TR_LD; break;
        case 2: m_CondTrace = COND_TR_ST; break;
        case 3: m_CondTrace = COND_TR_LDST; break;
        case 7: m_CondTrace = COND_TR_ALL; break;
        }
        m_condTraceCalc = true;
    }
    return m_CondTrace;
}

inline const bool EtmV4Config::enabledTS() const
{
    return ((m_cfg.reg_configr & (0x1 << 11)) != 0);
}

inline const bool EtmV4Config::enabledRetStack() const
{
    return ((m_cfg.reg_configr & (0x1 << 12)) != 0);
}

inline const bool EtmV4Config::enabledQE() const
{
       return ((m_cfg.reg_configr & (0x3 << 13)) != 0);
}

/** @}*/
/** @}*/

#endif // ARM_TRC_CMP_CFG_ETMV4_H_INCLUDED

/* End of File trc_cmp_cfg_etmv4.h */
