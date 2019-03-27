/* rep-cache-db.sql -- schema for use in rep-caching
 *   This is intended for use with SQLite 3
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

-- STMT_CREATE_SCHEMA_V1
/* A table mapping representation hashes to locations in a rev file. */
CREATE TABLE rep_cache (
  hash TEXT NOT NULL PRIMARY KEY,
  revision INTEGER NOT NULL,
  offset INTEGER NOT NULL,
  size INTEGER NOT NULL,
  expanded_size INTEGER NOT NULL
  );

PRAGMA USER_VERSION = 1;

-- STMT_CREATE_SCHEMA_V2
/* A table mapping representation hashes to locations in a rev file.
   Same as in V1 schema, except that it uses the `WITHOUT ROWID` optimization:
   https://sqlite.org/withoutrowid.html

   Note that this optimization is only supported starting from SQLite version
   3.8.2 (2013-12-06).  To keep compatibility with existing binaries, it is
   only used for newer filesystem formats that were released together with
   bumping the minimum required SQLite version.
 */
CREATE TABLE rep_cache (
  hash TEXT NOT NULL PRIMARY KEY,
  revision INTEGER NOT NULL,
  offset INTEGER NOT NULL,
  size INTEGER NOT NULL,
  expanded_size INTEGER NOT NULL
  ) WITHOUT ROWID;

PRAGMA USER_VERSION = 2;

-- STMT_GET_REP
/* Works for both V1 and V2 schemas. */
SELECT revision, offset, size, expanded_size
FROM rep_cache
WHERE hash = ?1

-- STMT_SET_REP
/* Works for both V1 and V2 schemas. */
INSERT OR FAIL INTO rep_cache (hash, revision, offset, size, expanded_size)
VALUES (?1, ?2, ?3, ?4, ?5)

-- STMT_GET_REPS_FOR_RANGE
/* Works for both V1 and V2 schemas. */
SELECT hash, revision, offset, size, expanded_size
FROM rep_cache
WHERE revision >= ?1 AND revision <= ?2

-- STMT_GET_MAX_REV
/* Works for both V1 and V2 schemas. */
SELECT MAX(revision)
FROM rep_cache

-- STMT_DEL_REPS_YOUNGER_THAN_REV
/* Works for both V1 and V2 schemas. */
DELETE FROM rep_cache
WHERE revision > ?1

/* An INSERT takes an SQLite reserved lock that prevents other writes
   but doesn't block reads.  The incomplete transaction means that no
   permanent change is made to the database and the transaction is
   removed when the database is closed.

   Works for both V1 and V2 schemas.  */
-- STMT_LOCK_REP
BEGIN TRANSACTION;
INSERT INTO rep_cache VALUES ('dummy', 0, 0, 0, 0)

-- STMT_UNLOCK_REP
/* Works for both V1 and V2 schemas. */
ROLLBACK TRANSACTION;
