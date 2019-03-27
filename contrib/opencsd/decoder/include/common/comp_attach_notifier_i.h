/*!
 * \file       comp_attach_notifier_i.h
 * \brief      OpenCSD : Component attach point notifier interface.
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

#ifndef ARM_COMP_ATTACH_NOTIFIER_I_H_INCLUDED
#define ARM_COMP_ATTACH_NOTIFIER_I_H_INCLUDED

/*!
 * @class IComponentAttachNotifier  
 * @addtogroup ocsd_infrastructure 
 * @brief Notification interface for attachment.
 * 
 *  Interface to the componentAttachPt classes that allow notification on component 
 *  connect and disconnect.
 */
class IComponentAttachNotifier {
public:
    IComponentAttachNotifier() {};  /**< Default interface constructor */
    virtual ~IComponentAttachNotifier() {}; /**< Default interface destructor */

    /*!
     * Callback called by the componentAttachPt() classes when a component is attached 
     * to or detached from the attach point.
     *
     * @param num_attached : number of remaining components attached to the point after the 
     *                       operation that triggered the notification.
     */
    virtual void attachNotify(const int num_attached) = 0;
};

#endif // ARM_COMP_ATTACH_NOTIFIER_I_H_INCLUDED

/* End of File comp_attach_notifier_i.h */
