/*
 * winservice.h : Public definitions for Windows Service support
 *
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */

#ifndef WINSERVICE_H
#define WINSERVICE_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#ifdef WIN32

/* Connects to the Windows Service Control Manager and allows this
   process to run as a service.  This function can only succeed if the
   process was started by the SCM, not directly by a user.  After this
   call succeeds, the service should perform whatever work it needs to
   start the service, and then the service should call
   winservice_running() (if no errors occurred) or winservice_stop()
   (if something failed during startup). */
svn_error_t *winservice_start(void);

/* Notifies the SCM that the service is now running.  The caller must
   already have called winservice_start successfully. */
void winservice_running(void);

/* This function is called by the SCM in an arbitrary thread when the
   SCM wants the service to stop.  The implementation of this function
   can return immediately; all that is necessary is that the service
   eventually stop in response. */
void winservice_notify_stop(void);

/* Evaluates to TRUE if the SCM has requested that the service stop.
   This allows for the service to poll, in addition to being notified
   in the winservice_notify_stop callback. */
svn_boolean_t winservice_is_stopping(void);

#endif /* WIN32 */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* WINSERVICE_H */
