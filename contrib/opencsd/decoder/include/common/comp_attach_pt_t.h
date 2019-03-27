/*!
 * \file       comp_attach_pt_t.h
 * \brief      OpenCSD : Component attachment point interface class.
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

#ifndef ARM_COMP_ATTACH_PT_T_H_INCLUDED
#define ARM_COMP_ATTACH_PT_T_H_INCLUDED

#include <vector>
#include "opencsd/ocsd_if_types.h"

/** @defgroup ocsd_infrastructure  OpenCSD Library : Library Component Infrastructure

    @brief Classes providing library infrastructure and auxilary functionality
@{*/

#include "comp_attach_notifier_i.h"

/*!
 * @class componentAttachPt 
 * @brief Single component interface pointer attachment point.
 * 
 *  This is a class template to standardise the connections between decode components.
 *
 *  An attachment point connects a component interface pointer to the component providing the 
 *  attachment point.
 *
 *  This attachment point implementation allows a single interface to be connected.
 * 
 */
template <class T> 
class componentAttachPt {
public:
    componentAttachPt();    /**< Default constructor */
    virtual ~componentAttachPt();   /**< Default destructor */
    
    /*!
     * Attach an interface of type T to the attachment point. 
     *
     * @param component : interface to attach.
     *
     * @return  ocsd_err_t  : OCSD_OK if successful, OCSD_ERR_ATTACH_TOO_MANY if too many connections.
     */
    virtual ocsd_err_t attach(T* component);

    /*!
     * Detach component from the attachment point.
     *
     * @param component : Component to detach.
     *
     * @return virtual ocsd_err_t  : OCSD_OK if successful, OCSD_ERR_ATTACH_COMP_NOT_FOUND if no match to component.
     */
    virtual ocsd_err_t detach(T* component);


    // detach current first if anything attached, connect supplied pointer, remain unattached if pointer 0
    virtual ocsd_err_t replace_first(T* component);

    /*!
     * Detach all components.
     */
    virtual void detach_all();

    /*!
     * Return the current (first) attached interface pointer.
     * Will return 0 if nothing attached or the attachment point is disabled.
     *
     * @return  T*  : Current Interface pointer of type T or 0.
     */
    virtual T* first(); 

    /*!
     * Return the next attached interface. 
     * The componentAttachPt base implmentation will always return 0 as only a single attachment is possible
     *
     * @return  T*  :  Always returns 0.
     */
    virtual T* next();

    /*!
     * Returns the number of interface pointers attached to this attachment point.
     *
     * @return  int  :  number of component interfaces attached.
     */
    virtual int num_attached();

    /*!
     * Attach a notifier interface to the attachment point. Will call back on this interface whenever 
     * a component is attached or detached.
     *
     * @param *notifier : pointer to the IComponentAttachNotifier interface.
     */
    void set_notifier(IComponentAttachNotifier *notifier);

    /* enable state does not affect attach / detach, but can be used to filter access to interfaces */
    const bool enabled() const; /**< return the enabled flag. */
    void set_enabled(const bool enable);


    /*!
     * Check to see if any attachements. Will return attach state independent of enable state.
     *
     * @return const bool  : true if attachment.
     */
    const bool hasAttached() const {  return m_hasAttached; };


    /*!
     * Return both the attachment and enabled state.
     *
     * @return const bool  : true if both has attachment and is enabled.
     */
    const bool hasAttachedAndEnabled() const { return  m_hasAttached && m_enabled; };

protected:
    bool m_enabled; /**< Flag to indicate if the attachment point is enabled. */
    bool m_hasAttached;      /**< Flag indicating at least one attached interface */
    IComponentAttachNotifier *m_notifier;   /**< Optional attachement notifier interface. */
    T *m_comp;  /**< pointer to the single attached interface */
};



template<class T> componentAttachPt<T>::componentAttachPt()
{
    m_comp = 0;
    m_notifier = 0;
    m_enabled = true;
    m_hasAttached = false;
}

template<class T> componentAttachPt<T>::~componentAttachPt()
{
    detach_all();
}


template<class T> ocsd_err_t componentAttachPt<T>::attach(T* component)
{
    if(m_comp != 0)
            return OCSD_ERR_ATTACH_TOO_MANY;
    m_comp = component;
    if(m_notifier) m_notifier->attachNotify(1);
    m_hasAttached = true;
    return OCSD_OK;
}

template<class T>  ocsd_err_t componentAttachPt<T>::replace_first(T* component)
{
    if(m_hasAttached)
        detach(m_comp);

    if(component == 0)
        return OCSD_OK;
    
    return attach(component);
}

template<class T> ocsd_err_t componentAttachPt<T>::detach(T* component)
{    
    if(m_comp != component)
        return OCSD_ERR_ATTACH_COMP_NOT_FOUND;
    m_comp = 0;
    m_hasAttached = false;
    if(m_notifier) m_notifier->attachNotify(0);
    return OCSD_OK;
}

template<class T> T* componentAttachPt<T>::first()
{
    return (m_enabled) ? m_comp : 0;
}

template<class T> T* componentAttachPt<T>::next()
{
    return 0;
}

template<class T> int componentAttachPt<T>::num_attached()
{
    return ((m_comp != 0)  ? 1 : 0);
}

template<class T> void componentAttachPt<T>::detach_all()
{
    m_comp = 0;
    m_hasAttached = false;
    if(m_notifier) m_notifier->attachNotify(0);
}

template<class T> void componentAttachPt<T>::set_notifier(IComponentAttachNotifier *notifier)
{
    m_notifier = notifier;
}

template<class T> const bool componentAttachPt<T>::enabled() const
{
    return m_enabled;
}

template<class T> void componentAttachPt<T>::set_enabled(const bool enable)
{
    m_enabled = enable;
}


/** @}*/

#endif // ARM_COMP_ATTACH_PT_T_H_INCLUDED

/* End of File comp_attach_pt_t.h */
