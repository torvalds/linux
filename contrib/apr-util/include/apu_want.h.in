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

#include "apu.h"        /* configuration data */

/**
 * @file apu_want.h
 * @brief APR Standard Headers Support
 *
 * <PRE>
 * Features:
 *
 *   APU_WANT_DB:       <@apu_db_header@>
 *
 * Typical usage:
 *
 *   #define APU_WANT_DB
 *   #include "apu_want.h"
 *
 * The appropriate headers will be included.
 *
 * Note: it is safe to use this in a header (it won't interfere with other
 *       headers' or source files' use of apu_want.h)
 * </PRE>
 */

/* --------------------------------------------------------------------- */

#ifdef APU_WANT_DB

#if APU_HAVE_DB
#include <@apu_db_header@>
#endif

#undef APU_WANT_DB
#endif

/* --------------------------------------------------------------------- */
