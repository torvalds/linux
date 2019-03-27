/* sqlite.sql -- queries used by the Subversion SQLite interface
 *     This is intended for use with SQLite 3
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

-- STMT_INTERNAL_SAVEPOINT_SVN
SAVEPOINT svn

-- STMT_INTERNAL_RELEASE_SAVEPOINT_SVN
RELEASE SAVEPOINT svn

-- STMT_INTERNAL_ROLLBACK_TO_SAVEPOINT_SVN
ROLLBACK TO SAVEPOINT svn

-- STMT_INTERNAL_BEGIN_TRANSACTION
BEGIN TRANSACTION

-- STMT_INTERNAL_BEGIN_IMMEDIATE_TRANSACTION
BEGIN IMMEDIATE TRANSACTION

-- STMT_INTERNAL_COMMIT_TRANSACTION
COMMIT TRANSACTION

-- STMT_INTERNAL_ROLLBACK_TRANSACTION
ROLLBACK TRANSACTION

/* Dummmy statement to determine the number of internal statements */
-- STMT_INTERNAL_LAST
;
