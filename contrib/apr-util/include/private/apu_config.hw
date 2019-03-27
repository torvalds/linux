/* Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* 
 * Note: This is a Windows specific version of apu_config.hw. It is copied
 * as apu_config.h at the start of a Windows build.
 */

#ifdef WIN32

#ifndef APU_CONFIG_H
#define APU_CONFIG_H

/* Compile win32 with DSO support for .dll builds */
#ifdef APU_DECLARE_STATIC
#define APU_DSO_BUILD           0
#else
#define APU_DSO_BUILD           1
#endif

/* Presume a standard, modern (5.x) mysql sdk/
#define HAVE_MY_GLOBAL_H        1

/* my_sys.h is broken on VC/Win32, and apparently not required */
/* #undef HAVE_MY_SYS_H           0 */

/*
 * Windows does not have GDBM, and we always use the bundled (new) Expat
 */

/* Define if you have the gdbm library (-lgdbm).  */
/* #undef HAVE_LIBGDBM */

/* define if Expat 1.0 or 1.1 was found */
/* #undef APR_HAVE_OLD_EXPAT */


#endif /* APU_CONFIG_H */
#endif /* WIN32 */
