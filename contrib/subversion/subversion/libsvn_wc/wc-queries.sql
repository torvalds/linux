/* wc-queries.sql -- queries used to interact with the wc-metadata
 *                   SQLite database
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

/* ------------------------------------------------------------------------- */

/* these are used in wc_db.c  */

-- STMT_SELECT_NODE_INFO
SELECT op_depth, repos_id, repos_path, presence, kind, revision, checksum,
  translated_size, changed_revision, changed_date, changed_author, depth,
  symlink_target, last_mod_time, properties, moved_here, inherited_props,
  moved_to
FROM nodes
WHERE wc_id = ?1 AND local_relpath = ?2
ORDER BY op_depth DESC

-- STMT_SELECT_NODE_INFO_WITH_LOCK
SELECT op_depth, nodes.repos_id, nodes.repos_path, presence, kind, revision,
  checksum, translated_size, changed_revision, changed_date, changed_author,
  depth, symlink_target, last_mod_time, properties, moved_here,
  inherited_props,
  /* All the columns until now must match those returned by
     STMT_SELECT_NODE_INFO. The implementation of svn_wc__db_read_info()
     assumes that these columns are followed by the lock information) */
  lock_token, lock_owner, lock_comment, lock_date
FROM nodes
LEFT OUTER JOIN lock ON nodes.repos_id = lock.repos_id
  AND nodes.repos_path = lock.repos_relpath AND nodes.op_depth=0
WHERE wc_id = ?1 AND local_relpath = ?2
ORDER BY op_depth DESC

-- STMT_SELECT_BASE_NODE
SELECT repos_id, repos_path, presence, kind, revision, checksum,
  translated_size, changed_revision, changed_date, changed_author, depth,
  symlink_target, last_mod_time, properties, file_external
FROM nodes
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = 0

-- STMT_SELECT_BASE_NODE_WITH_LOCK
SELECT nodes.repos_id, nodes.repos_path, presence, kind, revision,
  checksum, translated_size, changed_revision, changed_date, changed_author,
  depth, symlink_target, last_mod_time, properties, file_external,
  /* All the columns until now must match those returned by
     STMT_SELECT_BASE_NODE. The implementation of svn_wc__db_base_get_info()
     assumes that these columns are followed by the lock information) */
  lock_token, lock_owner, lock_comment, lock_date
FROM nodes
LEFT OUTER JOIN lock ON nodes.repos_id = lock.repos_id
  AND nodes.repos_path = lock.repos_relpath
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = 0

-- STMT_SELECT_BASE_CHILDREN_INFO
SELECT local_relpath, nodes.repos_id, nodes.repos_path, presence, kind,
  revision, depth, file_external
FROM nodes
WHERE wc_id = ?1 AND parent_relpath = ?2 AND op_depth = 0

-- STMT_SELECT_BASE_CHILDREN_INFO_LOCK
SELECT local_relpath, nodes.repos_id, nodes.repos_path, presence, kind,
  revision, depth, file_external,
  lock_token, lock_owner, lock_comment, lock_date
FROM nodes
LEFT OUTER JOIN lock ON nodes.repos_id = lock.repos_id
  AND nodes.repos_path = lock.repos_relpath
WHERE wc_id = ?1 AND parent_relpath = ?2 AND op_depth = 0


-- STMT_SELECT_WORKING_NODE
SELECT op_depth, presence, kind, checksum, translated_size,
  changed_revision, changed_date, changed_author, depth, symlink_target,
  repos_id, repos_path, revision,
  moved_here, moved_to, last_mod_time, properties
FROM nodes
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth > 0
ORDER BY op_depth DESC
LIMIT 1

-- STMT_SELECT_DEPTH_NODE
SELECT repos_id, repos_path, presence, kind, revision, checksum,
  translated_size, changed_revision, changed_date, changed_author, depth,
  symlink_target, properties, moved_to, moved_here
FROM nodes
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = ?3

-- STMT_SELECT_LOWEST_WORKING_NODE
SELECT op_depth, presence, kind, moved_to
FROM nodes
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth > ?3
ORDER BY op_depth
LIMIT 1

-- STMT_SELECT_HIGHEST_WORKING_NODE
SELECT op_depth
FROM nodes
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth < ?3
ORDER BY op_depth DESC
LIMIT 1

-- STMT_SELECT_ACTUAL_NODE
SELECT changelist, properties, conflict_data
FROM actual_node
WHERE wc_id = ?1 AND local_relpath = ?2

-- STMT_SELECT_NODE_CHILDREN_INFO
/* Getting rows in an advantageous order using
     ORDER BY local_relpath, op_depth DESC
   doesn't work as the index is created without the DESC keyword.
   Using both local_relpath and op_depth descending does work without any
   performance penalty. */
SELECT op_depth, nodes.repos_id, nodes.repos_path, presence, kind, revision,
  checksum, translated_size, changed_revision, changed_date, changed_author,
  depth, symlink_target, last_mod_time, properties, lock_token, lock_owner,
  lock_comment, lock_date, local_relpath, moved_here, moved_to, file_external
FROM nodes
LEFT OUTER JOIN lock ON nodes.repos_id = lock.repos_id
  AND nodes.repos_path = lock.repos_relpath AND nodes.op_depth = 0
WHERE wc_id = ?1 AND parent_relpath = ?2
ORDER BY local_relpath DESC, op_depth DESC

-- STMT_SELECT_BASE_NODE_CHILDREN_INFO
/* See above re: result ordering. The results of this query must be in
the same order as returned by STMT_SELECT_NODE_CHILDREN_INFO, because
read_children_info expects them to be. */
SELECT op_depth, nodes.repos_id, nodes.repos_path, presence, kind, revision,
  checksum, translated_size, changed_revision, changed_date, changed_author,
  depth, symlink_target, last_mod_time, properties, lock_token, lock_owner,
  lock_comment, lock_date, local_relpath, moved_here, moved_to, file_external
FROM nodes
LEFT OUTER JOIN lock ON nodes.repos_id = lock.repos_id
  AND nodes.repos_path = lock.repos_relpath
WHERE wc_id = ?1 AND parent_relpath = ?2 AND op_depth = 0
ORDER BY local_relpath DESC

-- STMT_SELECT_NODE_CHILDREN_WALKER_INFO
SELECT local_relpath, op_depth, presence, kind
FROM nodes_current
WHERE wc_id = ?1 AND parent_relpath = ?2
ORDER BY local_relpath

-- STMT_SELECT_ACTUAL_CHILDREN_INFO
SELECT local_relpath, changelist, properties, conflict_data
FROM actual_node
WHERE wc_id = ?1 AND parent_relpath = ?2

-- STMT_SELECT_REPOSITORY_BY_ID
SELECT root, uuid FROM repository WHERE id = ?1

-- STMT_SELECT_WCROOT_NULL
SELECT id FROM wcroot WHERE local_abspath IS NULL

-- STMT_SELECT_REPOSITORY
SELECT id FROM repository WHERE root = ?1

-- STMT_INSERT_REPOSITORY
INSERT INTO repository (root, uuid) VALUES (?1, ?2)

-- STMT_INSERT_NODE
INSERT OR REPLACE INTO nodes (
  wc_id, local_relpath, op_depth, parent_relpath, repos_id, repos_path,
  revision, presence, depth, kind, changed_revision, changed_date,
  changed_author, checksum, properties, translated_size, last_mod_time,
  dav_cache, symlink_target, file_external, moved_to, moved_here,
  inherited_props)
VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14,
        ?15, ?16, ?17, ?18, ?19, ?20, ?21, ?22, ?23)

-- STMT_SELECT_WORKING_PRESENT
SELECT local_relpath, kind, checksum, translated_size, last_mod_time
FROM nodes n
WHERE wc_id = ?1
  AND IS_STRICT_DESCENDANT_OF(local_relpath, ?2)
  AND presence in (MAP_NORMAL, MAP_INCOMPLETE)
  AND op_depth = (SELECT MAX(op_depth)
                  FROM NODES w
                  WHERE w.wc_id = ?1
                    AND w.local_relpath = n.local_relpath)
ORDER BY local_relpath DESC

-- STMT_DELETE_NODE_RECURSIVE
DELETE FROM NODES
WHERE wc_id = ?1
  AND IS_STRICT_DESCENDANT_OF(local_relpath, ?2)

-- STMT_DELETE_NODE
DELETE
FROM NODES
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = ?3

-- STMT_DELETE_ACTUAL_FOR_BASE_RECURSIVE
/* The ACTUAL_NODE applies to BASE, unless there is in at least one op_depth
   a WORKING node that could have a conflict */
DELETE FROM actual_node
WHERE wc_id = ?1 AND IS_STRICT_DESCENDANT_OF(local_relpath, ?2)
  AND EXISTS(SELECT 1 FROM NODES b
             WHERE b.wc_id = ?1
               AND b.local_relpath = actual_node.local_relpath
               AND op_depth = 0)
  AND NOT EXISTS(SELECT 1 FROM NODES w
                 WHERE w.wc_id = ?1
                   AND w.local_relpath = actual_node.local_relpath
                   AND op_depth > 0
                   AND presence in (MAP_NORMAL, MAP_INCOMPLETE, MAP_NOT_PRESENT))

-- STMT_DELETE_WORKING_BASE_DELETE
DELETE FROM nodes
WHERE wc_id = ?1 AND local_relpath = ?2
  AND presence = MAP_BASE_DELETED
  AND op_depth > ?3
  AND op_depth = (SELECT MIN(op_depth) FROM nodes n
                    WHERE n.wc_id = ?1
                      AND n.local_relpath = nodes.local_relpath
                      AND op_depth > ?3)

-- STMT_DELETE_WORKING_BASE_DELETE_RECURSIVE
DELETE FROM nodes
WHERE wc_id = ?1 AND IS_STRICT_DESCENDANT_OF(local_relpath, ?2)
  AND presence = MAP_BASE_DELETED
  AND op_depth > ?3
  AND op_depth = (SELECT MIN(op_depth) FROM nodes n
                    WHERE n.wc_id = ?1
                      AND n.local_relpath = nodes.local_relpath
                      AND op_depth > ?3)

-- STMT_DELETE_WORKING_RECURSIVE
DELETE FROM nodes
WHERE wc_id = ?1 AND IS_STRICT_DESCENDANT_OF(local_relpath, ?2)
  AND op_depth > 0

-- STMT_DELETE_BASE_RECURSIVE
DELETE FROM nodes
WHERE wc_id = ?1 AND (local_relpath = ?2 
                      OR IS_STRICT_DESCENDANT_OF(local_relpath, ?2))
  AND op_depth = 0

-- STMT_DELETE_WORKING_OP_DEPTH
DELETE FROM nodes
WHERE wc_id = ?1
  AND (local_relpath = ?2 OR IS_STRICT_DESCENDANT_OF(local_relpath, ?2))
  AND op_depth = ?3

/* Full layer replacement check code for handling moves
The op_root must exist (or there is no layer to replace) and an op-root
   always has presence 'normal' */
-- STMT_SELECT_LAYER_FOR_REPLACE
SELECT s.local_relpath, s.kind,
  RELPATH_SKIP_JOIN(?2, ?4, s.local_relpath) drp, 'normal'
FROM nodes s
WHERE s.wc_id = ?1 AND s.local_relpath = ?2 AND s.op_depth = ?3
UNION ALL
SELECT s.local_relpath, s.kind,
  RELPATH_SKIP_JOIN(?2, ?4, s.local_relpath) drp, d.presence
FROM nodes s
LEFT OUTER JOIN nodes d ON d.wc_id= ?1 AND d.op_depth = ?5
     AND d.local_relpath = drp
WHERE s.wc_id = ?1
  AND IS_STRICT_DESCENDANT_OF(s.local_relpath, ?2)
  AND s.op_depth = ?3
ORDER BY s.local_relpath

-- STMT_SELECT_DESCENDANTS_OP_DEPTH_RV
SELECT local_relpath, kind
FROM nodes
WHERE wc_id = ?1
  AND IS_STRICT_DESCENDANT_OF(local_relpath, ?2)
  AND op_depth = ?3
  AND presence in (MAP_NORMAL, MAP_INCOMPLETE)
ORDER BY local_relpath DESC

-- STMT_COPY_NODE_MOVE
INSERT OR REPLACE INTO nodes (
    wc_id, local_relpath, op_depth, parent_relpath, repos_id, repos_path,
    revision, presence, depth, kind, changed_revision, changed_date,
    changed_author, checksum, properties, translated_size, last_mod_time,
    symlink_target, moved_here, moved_to )
SELECT
    s.wc_id, ?4 /*local_relpath */, ?5 /*op_depth*/, ?6 /* parent_relpath */,
    s.repos_id,
    s.repos_path, s.revision, s.presence, s.depth, s.kind, s.changed_revision,
    s.changed_date, s.changed_author, s.checksum, s.properties,
    CASE WHEN d.checksum=s.checksum THEN d.translated_size END,
    CASE WHEN d.checksum=s.checksum THEN d.last_mod_time END,
    s.symlink_target, 1, d.moved_to
FROM nodes s
LEFT JOIN nodes d ON d.wc_id=?1 AND d.local_relpath=?4 AND d.op_depth=?5
WHERE s.wc_id = ?1 AND s.local_relpath = ?2 AND s.op_depth = ?3

-- STMT_SELECT_NO_LONGER_MOVED_RV
SELECT d.local_relpath, RELPATH_SKIP_JOIN(?2, ?4, d.local_relpath) srp,
       b.presence, b.op_depth
FROM nodes d
LEFT OUTER JOIN nodes b ON b.wc_id = ?1 AND b.local_relpath = d.local_relpath
            AND b.op_depth = (SELECT MAX(x.op_depth) FROM nodes x
                              WHERE x.wc_id = ?1
                                AND x.local_relpath = b.local_relpath
                                AND x.op_depth < ?3)
WHERE d.wc_id = ?1
  AND IS_STRICT_DESCENDANT_OF(d.local_relpath, ?2)
  AND d.op_depth = ?3
  AND NOT EXISTS(SELECT * FROM nodes s
                 WHERE s.wc_id = ?1
                   AND s.local_relpath = srp
                   AND s.op_depth = ?5)
ORDER BY d.local_relpath DESC

-- STMT_SELECT_OP_DEPTH_CHILDREN
SELECT local_relpath, kind FROM nodes
WHERE wc_id = ?1
  AND parent_relpath = ?2
  AND op_depth = ?3
  AND presence != MAP_BASE_DELETED
  AND file_external is NULL
ORDER BY local_relpath

-- STMT_SELECT_OP_DEPTH_CHILDREN_EXISTS
SELECT local_relpath, kind FROM nodes
WHERE wc_id = ?1
  AND parent_relpath = ?2
  AND op_depth = ?3
  AND presence IN (MAP_NORMAL, MAP_INCOMPLETE)
ORDER BY local_relpath

/* Used by non-recursive revert to detect higher level children, and
   actual-only rows that would be left orphans, if the revert
   proceeded. */
-- STMT_SELECT_GE_OP_DEPTH_CHILDREN
SELECT 1 FROM nodes
WHERE wc_id = ?1 AND parent_relpath = ?2
  AND (op_depth > ?3 OR (op_depth = ?3
                         AND presence IN (MAP_NORMAL, MAP_INCOMPLETE)))
UNION ALL
SELECT 1 FROM ACTUAL_NODE a
WHERE wc_id = ?1 AND parent_relpath = ?2
  AND NOT EXISTS (SELECT 1 FROM nodes n
                   WHERE wc_id = ?1 AND n.local_relpath = a.local_relpath)

/* Delete the nodes shadowed by local_relpath. Not valid for the wc-root */
-- STMT_DELETE_SHADOWED_RECURSIVE
DELETE FROM nodes
WHERE wc_id = ?1
  AND IS_STRICT_DESCENDANT_OF(local_relpath, ?2)
  AND (op_depth < ?3
       OR (op_depth = ?3 AND presence = MAP_BASE_DELETED))

-- STMT_CLEAR_MOVED_TO_FROM_DEST
UPDATE NODES SET moved_to = NULL
WHERE wc_id = ?1
  AND moved_to = ?2

/* Get not-present descendants of a copied node. Not valid for the wc-root */
-- STMT_SELECT_NOT_PRESENT_DESCENDANTS
SELECT local_relpath FROM nodes
WHERE wc_id = ?1 AND op_depth = ?3
  AND IS_STRICT_DESCENDANT_OF(local_relpath, ?2)
  AND presence = MAP_NOT_PRESENT

-- STMT_COMMIT_DESCENDANTS_TO_BASE
UPDATE NODES SET op_depth = 0,
                 repos_id = ?4,
                 repos_path = RELPATH_SKIP_JOIN(?2, ?5, local_relpath),
                 revision = ?6,
                 dav_cache = NULL,
                 moved_here = NULL,
                 moved_to = NULL,
                 presence = CASE presence
                              WHEN MAP_NORMAL THEN MAP_NORMAL
                              WHEN MAP_EXCLUDED THEN MAP_EXCLUDED
                              ELSE MAP_NOT_PRESENT
                            END
WHERE wc_id = ?1
  AND IS_STRICT_DESCENDANT_OF(local_relpath, ?2)
  AND op_depth = ?3

-- STMT_SELECT_NODE_CHILDREN
/* Return all paths that are children of the directory (?1, ?2) in any
   op-depth, including children of any underlying, replaced directories. */
SELECT DISTINCT local_relpath FROM nodes
WHERE wc_id = ?1 AND parent_relpath = ?2
ORDER BY local_relpath

-- STMT_SELECT_WORKING_CHILDREN
/* Return all paths that are children of the working version of the
   directory (?1, ?2).  A given path is not included just because it is a
   child of an underlying (replaced) directory, it has to be in the
   working version of the directory. */
SELECT DISTINCT local_relpath FROM nodes
WHERE wc_id = ?1 AND parent_relpath = ?2
  AND (op_depth > (SELECT MAX(op_depth) FROM nodes
                   WHERE wc_id = ?1 AND local_relpath = ?2)
       OR
       (op_depth = (SELECT MAX(op_depth) FROM nodes
                    WHERE wc_id = ?1 AND local_relpath = ?2)
        AND presence IN (MAP_NORMAL, MAP_INCOMPLETE)))
ORDER BY local_relpath

-- STMT_SELECT_BASE_NOT_PRESENT_CHILDREN
SELECT local_relpath FROM nodes
WHERE wc_id = ?1 AND parent_relpath = ?2 AND op_depth = 0
  AND presence = MAP_NOT_PRESENT
ORDER BY local_relpath

-- STMT_SELECT_NODE_PROPS
SELECT properties, presence FROM nodes
WHERE wc_id = ?1 AND local_relpath = ?2
ORDER BY op_depth DESC

-- STMT_SELECT_ACTUAL_PROPS
SELECT properties FROM actual_node
WHERE wc_id = ?1 AND local_relpath = ?2

-- STMT_UPDATE_ACTUAL_PROPS
UPDATE actual_node SET properties = ?3
WHERE wc_id = ?1 AND local_relpath = ?2

-- STMT_INSERT_ACTUAL_PROPS
INSERT INTO actual_node (wc_id, local_relpath, parent_relpath, properties)
VALUES (?1, ?2, ?3, ?4)

-- STMT_INSERT_LOCK
INSERT OR REPLACE INTO lock
(repos_id, repos_relpath, lock_token, lock_owner, lock_comment,
 lock_date)
VALUES (?1, ?2, ?3, ?4, ?5, ?6)

/* Not valid for the working copy root */
-- STMT_SELECT_BASE_NODE_LOCK_TOKENS_RECURSIVE
SELECT nodes.repos_id, nodes.repos_path, lock_token
FROM nodes
LEFT JOIN lock ON nodes.repos_id = lock.repos_id
  AND nodes.repos_path = lock.repos_relpath
WHERE wc_id = ?1 AND op_depth = 0
  AND IS_STRICT_DESCENDANT_OF(local_relpath, ?2)

-- STMT_INSERT_WCROOT
INSERT INTO wcroot (local_abspath)
VALUES (?1)

-- STMT_UPDATE_BASE_NODE_DAV_CACHE
UPDATE nodes SET dav_cache = ?3
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = 0

-- STMT_SELECT_BASE_DAV_CACHE
SELECT dav_cache FROM nodes
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = 0

-- STMT_SELECT_DELETION_INFO
SELECT b.presence, w.presence, w.op_depth, w.moved_to
FROM nodes w
LEFT JOIN nodes b ON b.wc_id = ?1 AND b.local_relpath = ?2 AND b.op_depth = 0
WHERE w.wc_id = ?1 AND w.local_relpath = ?2
  AND w.op_depth = (SELECT MAX(op_depth) FROM nodes d
                    WHERE d.wc_id = ?1 AND d.local_relpath = ?2
                      AND d.op_depth > 0)
LIMIT 1

-- STMT_SELECT_MOVED_TO_NODE
SELECT op_depth, moved_to
FROM nodes
WHERE wc_id = ?1 AND local_relpath = ?2 AND moved_to IS NOT NULL
ORDER BY op_depth DESC

-- STMT_SELECT_OP_DEPTH_MOVED_TO
SELECT op_depth, moved_to
FROM nodes
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth > ?3
  AND EXISTS(SELECT * from nodes
             WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = ?3
             AND presence IN (MAP_NORMAL, MAP_INCOMPLETE))
ORDER BY op_depth ASC
LIMIT 1

-- STMT_SELECT_MOVED_TO
SELECT moved_to
FROM nodes
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = ?3

-- STMT_SELECT_MOVED_BACK
SELECT u.local_relpath,
       u.presence, u.repos_id, u.repos_path, u.revision,
       l.presence, l.repos_id, l.repos_path, l.revision,
       u.moved_here, u.moved_to
FROM nodes u
LEFT OUTER JOIN nodes l ON l.wc_id = ?1
                       AND l.local_relpath = u.local_relpath
                       AND l.op_depth = ?3
WHERE u.wc_id = ?1
  AND u.local_relpath = ?2
  AND u.op_depth = ?4
UNION ALL
SELECT u.local_relpath,
       u.presence, u.repos_id, u.repos_path, u.revision,
       l.presence, l.repos_id, l.repos_path, l.revision,
       u.moved_here, NULL
FROM nodes u
LEFT OUTER JOIN nodes l ON l.wc_id=?1
                       AND l.local_relpath=u.local_relpath
                       AND l.op_depth=?3
WHERE u.wc_id = ?1
  AND IS_STRICT_DESCENDANT_OF(u.local_relpath, ?2)
  AND u.op_depth = ?4

-- STMT_DELETE_LOCK
DELETE FROM lock
WHERE repos_id = ?1 AND repos_relpath = ?2

-- STMT_DELETE_LOCK_RECURSIVELY
DELETE FROM lock
WHERE repos_id = ?1 AND (repos_relpath = ?2 OR IS_STRICT_DESCENDANT_OF(repos_relpath, ?2))

-- STMT_CLEAR_BASE_NODE_RECURSIVE_DAV_CACHE
UPDATE nodes SET dav_cache = NULL
WHERE dav_cache IS NOT NULL AND wc_id = ?1 AND op_depth = 0
  AND (local_relpath = ?2
       OR IS_STRICT_DESCENDANT_OF(local_relpath, ?2))

-- STMT_RECURSIVE_UPDATE_NODE_REPO
UPDATE nodes SET repos_id = ?4, dav_cache = NULL
/* ### The Sqlite optimizer needs help here ###
 * WHERE wc_id = ?1
 *   AND repos_id = ?3
 *   AND (local_relpath = ?2
 *        OR IS_STRICT_DESCENDANT_OF(local_relpath, ?2))*/
WHERE (wc_id = ?1 AND local_relpath = ?2 AND repos_id = ?3)
   OR (wc_id = ?1 AND IS_STRICT_DESCENDANT_OF(local_relpath, ?2)
       AND repos_id = ?3)


-- STMT_UPDATE_LOCK_REPOS_ID
UPDATE lock SET repos_id = ?2
WHERE repos_id = ?1

-- STMT_UPDATE_NODE_FILEINFO
UPDATE nodes SET translated_size = ?3, last_mod_time = ?4
WHERE wc_id = ?1 AND local_relpath = ?2
  AND op_depth = (SELECT MAX(op_depth) FROM nodes
                  WHERE wc_id = ?1 AND local_relpath = ?2)

-- STMT_INSERT_ACTUAL_CONFLICT
INSERT INTO actual_node (wc_id, local_relpath, conflict_data, parent_relpath)
VALUES (?1, ?2, ?3, ?4)

-- STMT_UPDATE_ACTUAL_CONFLICT
UPDATE actual_node SET conflict_data = ?3
WHERE wc_id = ?1 AND local_relpath = ?2

-- STMT_UPDATE_ACTUAL_CHANGELISTS
UPDATE actual_node SET changelist = ?3
WHERE wc_id = ?1
  AND (local_relpath = ?2 OR IS_STRICT_DESCENDANT_OF(local_relpath, ?2))
  AND local_relpath = (SELECT local_relpath FROM targets_list AS t
                       WHERE wc_id = ?1
                         AND t.local_relpath = actual_node.local_relpath
                         AND kind = MAP_FILE)

-- STMT_UPDATE_ACTUAL_CLEAR_CHANGELIST
UPDATE actual_node SET changelist = NULL
 WHERE wc_id = ?1 AND local_relpath = ?2

-- STMT_MARK_SKIPPED_CHANGELIST_DIRS
/* 7 corresponds to svn_wc_notify_skip */
INSERT INTO changelist_list (wc_id, local_relpath, notify, changelist)
SELECT wc_id, local_relpath, 7, ?3
FROM targets_list
WHERE wc_id = ?1
  AND (local_relpath = ?2 OR IS_STRICT_DESCENDANT_OF(local_relpath, ?2))
  AND kind = MAP_DIR

-- STMT_RESET_ACTUAL_WITH_CHANGELIST
REPLACE INTO actual_node (
  wc_id, local_relpath, parent_relpath, changelist)
VALUES (?1, ?2, ?3, ?4)

-- STMT_CREATE_CHANGELIST_LIST
DROP TABLE IF EXISTS changelist_list;
CREATE TEMPORARY TABLE changelist_list (
  wc_id  INTEGER NOT NULL,
  local_relpath TEXT NOT NULL,
  notify INTEGER NOT NULL,
  changelist TEXT NOT NULL,
  /* Order NOTIFY descending to make us show clears (27) before adds (26) */
  PRIMARY KEY (wc_id, local_relpath, notify DESC)
)

/* Create notify items for when a node is removed from a changelist and
   when a node is added to a changelist. Make sure nothing is notified
   if there were no changes.
*/
-- STMT_CREATE_CHANGELIST_TRIGGER
DROP TRIGGER IF EXISTS   trigger_changelist_list_change;
CREATE TEMPORARY TRIGGER trigger_changelist_list_change
BEFORE UPDATE ON actual_node
WHEN old.changelist IS NOT new.changelist
BEGIN
  /* 27 corresponds to svn_wc_notify_changelist_clear */
  INSERT INTO changelist_list(wc_id, local_relpath, notify, changelist)
  SELECT old.wc_id, old.local_relpath, 27, old.changelist
   WHERE old.changelist is NOT NULL;

  /* 26 corresponds to svn_wc_notify_changelist_set */
  INSERT INTO changelist_list(wc_id, local_relpath, notify, changelist)
  SELECT new.wc_id, new.local_relpath, 26, new.changelist
   WHERE new.changelist IS NOT NULL;
END

-- STMT_FINALIZE_CHANGELIST
DROP TRIGGER trigger_changelist_list_change;
DROP TABLE changelist_list;
DROP TABLE targets_list

-- STMT_SELECT_CHANGELIST_LIST
SELECT wc_id, local_relpath, notify, changelist
FROM changelist_list
ORDER BY wc_id, local_relpath ASC, notify DESC

-- STMT_CREATE_TARGETS_LIST
DROP TABLE IF EXISTS targets_list;
CREATE TEMPORARY TABLE targets_list (
  wc_id  INTEGER NOT NULL,
  local_relpath TEXT NOT NULL,
  parent_relpath TEXT,
  kind TEXT NOT NULL,
  PRIMARY KEY (wc_id, local_relpath)
  );
/* need more indicies? */

-- STMT_DROP_TARGETS_LIST
DROP TABLE targets_list

-- STMT_INSERT_TARGET
INSERT INTO targets_list(wc_id, local_relpath, parent_relpath, kind)
SELECT wc_id, local_relpath, parent_relpath, kind
FROM nodes_current
WHERE wc_id = ?1
  AND local_relpath = ?2

-- STMT_INSERT_TARGET_DEPTH_FILES
INSERT INTO targets_list(wc_id, local_relpath, parent_relpath, kind)
SELECT wc_id, local_relpath, parent_relpath, kind
FROM nodes_current
WHERE wc_id = ?1
  AND parent_relpath = ?2
  AND kind = MAP_FILE

-- STMT_INSERT_TARGET_DEPTH_IMMEDIATES
INSERT INTO targets_list(wc_id, local_relpath, parent_relpath, kind)
SELECT wc_id, local_relpath, parent_relpath, kind
FROM nodes_current
WHERE wc_id = ?1
  AND parent_relpath = ?2

-- STMT_INSERT_TARGET_DEPTH_INFINITY
INSERT INTO targets_list(wc_id, local_relpath, parent_relpath, kind)
SELECT wc_id, local_relpath, parent_relpath, kind
FROM nodes_current
WHERE wc_id = ?1
  AND IS_STRICT_DESCENDANT_OF(local_relpath, ?2)

-- STMT_INSERT_TARGET_WITH_CHANGELIST
INSERT INTO targets_list(wc_id, local_relpath, parent_relpath, kind)
SELECT N.wc_id, N.local_relpath, N.parent_relpath, N.kind
  FROM actual_node AS A JOIN nodes_current AS N
    ON A.wc_id = N.wc_id AND A.local_relpath = N.local_relpath
 WHERE N.wc_id = ?1
   AND N.local_relpath = ?2
   AND A.changelist = ?3

-- STMT_INSERT_TARGET_WITH_CHANGELIST_DEPTH_FILES
INSERT INTO targets_list(wc_id, local_relpath, parent_relpath, kind)
SELECT N.wc_id, N.local_relpath, N.parent_relpath, N.kind
  FROM actual_node AS A JOIN nodes_current AS N
    ON A.wc_id = N.wc_id AND A.local_relpath = N.local_relpath
 WHERE N.wc_id = ?1
   AND N.parent_relpath = ?2
   AND kind = MAP_FILE
   AND A.changelist = ?3

-- STMT_INSERT_TARGET_WITH_CHANGELIST_DEPTH_IMMEDIATES
INSERT INTO targets_list(wc_id, local_relpath, parent_relpath, kind)
SELECT N.wc_id, N.local_relpath, N.parent_relpath, N.kind
  FROM actual_node AS A JOIN nodes_current AS N
    ON A.wc_id = N.wc_id AND A.local_relpath = N.local_relpath
 WHERE N.wc_id = ?1
   AND N.parent_relpath = ?2
  AND A.changelist = ?3

-- STMT_INSERT_TARGET_WITH_CHANGELIST_DEPTH_INFINITY
INSERT INTO targets_list(wc_id, local_relpath, parent_relpath, kind)
SELECT N.wc_id, N.local_relpath, N.parent_relpath, N.kind
  FROM actual_node AS A JOIN nodes_current AS N
    ON A.wc_id = N.wc_id AND A.local_relpath = N.local_relpath
 WHERE N.wc_id = ?1
   AND IS_STRICT_DESCENDANT_OF(N.local_relpath, ?2)
   AND A.changelist = ?3

/* Only used by commented dump_targets() in wc_db.c */
/*-- STMT_SELECT_TARGETS
SELECT local_relpath, parent_relpath from targets_list*/

-- STMT_INSERT_ACTUAL_EMPTIES
INSERT OR IGNORE INTO actual_node (
     wc_id, local_relpath, parent_relpath)
SELECT wc_id, local_relpath, parent_relpath
FROM targets_list

-- STMT_INSERT_ACTUAL_EMPTIES_FILES
INSERT OR IGNORE INTO actual_node (
     wc_id, local_relpath, parent_relpath)
SELECT wc_id, local_relpath, parent_relpath
FROM targets_list
WHERE kind=MAP_FILE

-- STMT_DELETE_ACTUAL_EMPTY
DELETE FROM actual_node
WHERE wc_id = ?1 AND local_relpath = ?2
  AND properties IS NULL
  AND conflict_data IS NULL
  AND changelist IS NULL
  AND text_mod IS NULL
  AND older_checksum IS NULL
  AND right_checksum IS NULL
  AND left_checksum IS NULL

-- STMT_DELETE_ACTUAL_EMPTIES
DELETE FROM actual_node
WHERE wc_id = ?1
  AND (local_relpath = ?2 OR IS_STRICT_DESCENDANT_OF(local_relpath, ?2))
  AND properties IS NULL
  AND conflict_data IS NULL
  AND changelist IS NULL
  AND text_mod IS NULL
  AND older_checksum IS NULL
  AND right_checksum IS NULL
  AND left_checksum IS NULL

-- STMT_DELETE_BASE_NODE
DELETE FROM nodes
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = 0

-- STMT_DELETE_WORKING_NODE
DELETE FROM nodes
WHERE wc_id = ?1 AND local_relpath = ?2
  AND op_depth = (SELECT MAX(op_depth) FROM nodes
                  WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth > 0)

-- STMT_DELETE_LOWEST_WORKING_NODE
DELETE FROM nodes
WHERE wc_id = ?1 AND local_relpath = ?2
  AND op_depth = (SELECT MIN(op_depth) FROM nodes
                  WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth > ?3)
  AND presence = MAP_BASE_DELETED

-- STMT_DELETE_NODE_ALL_LAYERS
DELETE FROM nodes
WHERE wc_id = ?1 AND local_relpath = ?2

-- STMT_DELETE_NODES_ABOVE_DEPTH_RECURSIVE
DELETE FROM nodes
WHERE wc_id = ?1
  AND (local_relpath = ?2
       OR IS_STRICT_DESCENDANT_OF(local_relpath, ?2))
  AND op_depth >= ?3

-- STMT_DELETE_ACTUAL_NODE
DELETE FROM actual_node
WHERE wc_id = ?1 AND local_relpath = ?2

/* Will not delete recursive when run on the wcroot */
-- STMT_DELETE_ACTUAL_NODE_RECURSIVE
DELETE FROM actual_node
WHERE wc_id = ?1
  AND (local_relpath = ?2
       OR IS_STRICT_DESCENDANT_OF(local_relpath, ?2))

-- STMT_DELETE_ACTUAL_NODE_LEAVING_CHANGELIST
DELETE FROM actual_node
WHERE wc_id = ?1
  AND local_relpath = ?2
  AND (changelist IS NULL
       OR NOT EXISTS (SELECT 1 FROM nodes_current c
                      WHERE c.wc_id = ?1 AND c.local_relpath = ?2
                        AND c.kind = MAP_FILE))

-- STMT_DELETE_ACTUAL_NODE_LEAVING_CHANGELIST_RECURSIVE
DELETE FROM actual_node
WHERE wc_id = ?1
  AND (local_relpath = ?2
       OR IS_STRICT_DESCENDANT_OF(local_relpath, ?2))
  AND (changelist IS NULL
       OR NOT EXISTS (SELECT 1 FROM nodes_current c
                      WHERE c.wc_id = ?1
                        AND c.local_relpath = actual_node.local_relpath
                        AND c.kind = MAP_FILE))

-- STMT_CLEAR_ACTUAL_NODE_LEAVING_CHANGELIST
UPDATE actual_node
SET properties = NULL,
    text_mod = NULL,
    conflict_data = NULL,
    tree_conflict_data = NULL,
    older_checksum = NULL,
    left_checksum = NULL,
    right_checksum = NULL
WHERE wc_id = ?1 AND local_relpath = ?2

-- STMT_CLEAR_ACTUAL_NODE_LEAVING_CONFLICT
UPDATE actual_node
SET properties = NULL,
    text_mod = NULL,
    tree_conflict_data = NULL,
    older_checksum = NULL,
    left_checksum = NULL,
    right_checksum = NULL,
    changelist = NULL
WHERE wc_id = ?1 AND local_relpath = ?2

-- STMT_CLEAR_ACTUAL_NODE_LEAVING_CHANGELIST_RECURSIVE
UPDATE actual_node
SET properties = NULL,
    text_mod = NULL,
    conflict_data = NULL,
    tree_conflict_data = NULL,
    older_checksum = NULL,
    left_checksum = NULL,
    right_checksum = NULL
WHERE wc_id = ?1
  AND (local_relpath = ?2
       OR IS_STRICT_DESCENDANT_OF(local_relpath, ?2))

-- STMT_UPDATE_NODE_BASE_DEPTH
UPDATE nodes SET depth = ?3
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = 0
  AND kind=MAP_DIR
  AND presence IN (MAP_NORMAL, MAP_INCOMPLETE)

-- STMT_UPDATE_NODE_BASE_PRESENCE
UPDATE nodes SET presence = ?3
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = 0

-- STMT_UPDATE_BASE_NODE_PRESENCE_REVNUM_AND_REPOS_PATH
UPDATE nodes SET presence = ?3, revision = ?4, repos_path = ?5
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = 0

-- STMT_LOOK_FOR_WORK
SELECT id FROM work_queue LIMIT 1

-- STMT_INSERT_WORK_ITEM
INSERT INTO work_queue (work) VALUES (?1)

-- STMT_SELECT_WORK_ITEM
SELECT id, work FROM work_queue ORDER BY id LIMIT 1

-- STMT_DELETE_WORK_ITEM
DELETE FROM work_queue WHERE id = ?1

-- STMT_INSERT_OR_IGNORE_PRISTINE
INSERT OR IGNORE INTO pristine (checksum, md5_checksum, size, refcount)
VALUES (?1, ?2, ?3, 0)

-- STMT_INSERT_PRISTINE
INSERT INTO pristine (checksum, md5_checksum, size, refcount)
VALUES (?1, ?2, ?3, 0)

-- STMT_SELECT_PRISTINE
SELECT md5_checksum
FROM pristine
WHERE checksum = ?1

-- STMT_SELECT_PRISTINE_SIZE
SELECT size
FROM pristine
WHERE checksum = ?1 LIMIT 1

-- STMT_SELECT_PRISTINE_BY_MD5
SELECT checksum
FROM pristine
WHERE md5_checksum = ?1

-- STMT_SELECT_UNREFERENCED_PRISTINES
SELECT checksum
FROM pristine
WHERE refcount = 0

-- STMT_DELETE_PRISTINE_IF_UNREFERENCED
DELETE FROM pristine
WHERE checksum = ?1 AND refcount = 0

-- STMT_SELECT_COPY_PRISTINES
/* For the root itself */
SELECT n.checksum, md5_checksum, size
FROM nodes_current n
LEFT JOIN pristine p ON n.checksum = p.checksum
WHERE wc_id = ?1
  AND n.local_relpath = ?2
  AND n.checksum IS NOT NULL
UNION ALL
/* And all descendants */
SELECT n.checksum, md5_checksum, size
FROM nodes n
LEFT JOIN pristine p ON n.checksum = p.checksum
WHERE wc_id = ?1
  AND IS_STRICT_DESCENDANT_OF(n.local_relpath, ?2)
  AND op_depth >=
      (SELECT MAX(op_depth) FROM nodes WHERE wc_id = ?1 AND local_relpath = ?2)
  AND n.checksum IS NOT NULL

-- STMT_VACUUM
VACUUM

-- STMT_SELECT_CONFLICT_VICTIMS
SELECT local_relpath, conflict_data
FROM actual_node
WHERE wc_id = ?1 AND parent_relpath = ?2 AND
  NOT (conflict_data IS NULL)

-- STMT_INSERT_WC_LOCK
INSERT INTO wc_lock (wc_id, local_dir_relpath, locked_levels)
VALUES (?1, ?2, ?3)

-- STMT_SELECT_WC_LOCK
SELECT locked_levels FROM wc_lock
WHERE wc_id = ?1 AND local_dir_relpath = ?2

-- STMT_SELECT_ANCESTOR_WCLOCKS
SELECT local_dir_relpath, locked_levels FROM wc_lock
WHERE wc_id = ?1
  AND ((local_dir_relpath >= ?3 AND local_dir_relpath <= ?2)
       OR local_dir_relpath = '')

-- STMT_DELETE_WC_LOCK
DELETE FROM wc_lock
WHERE wc_id = ?1 AND local_dir_relpath = ?2

-- STMT_FIND_WC_LOCK
SELECT local_dir_relpath FROM wc_lock
WHERE wc_id = ?1
  AND IS_STRICT_DESCENDANT_OF(local_dir_relpath, ?2)

-- STMT_FIND_CONFLICT_DESCENDANT
SELECT 1 FROM actual_node
WHERE wc_id = ?1
  AND local_relpath > (?2 || '/')
  AND local_relpath < (?2 || '0') /* '0' = ascii('/') +1 */
  AND conflict_data IS NOT NULL
LIMIT 1

-- STMT_DELETE_WC_LOCK_ORPHAN
DELETE FROM wc_lock
WHERE wc_id = ?1 AND local_dir_relpath = ?2
AND NOT EXISTS (SELECT 1 FROM nodes
                 WHERE nodes.wc_id = ?1
                   AND nodes.local_relpath = wc_lock.local_dir_relpath)

-- STMT_DELETE_WC_LOCK_ORPHAN_RECURSIVE
DELETE FROM wc_lock
WHERE wc_id = ?1
  AND (local_dir_relpath = ?2
       OR IS_STRICT_DESCENDANT_OF(local_dir_relpath, ?2))
  AND NOT EXISTS (SELECT 1 FROM nodes
                   WHERE nodes.wc_id = ?1
                     AND nodes.local_relpath = wc_lock.local_dir_relpath)

-- STMT_APPLY_CHANGES_TO_BASE_NODE
/* translated_size and last_mod_time are not mentioned here because they will
   be tweaked after the working-file is installed. When we replace an existing
   BASE node (read: bump), preserve its file_external status. */
INSERT OR REPLACE INTO nodes (
  wc_id, local_relpath, op_depth, parent_relpath, repos_id, repos_path,
  revision, presence, depth, kind, changed_revision, changed_date,
  changed_author, checksum, properties, dav_cache, symlink_target,
  inherited_props, file_external )
VALUES (?1, ?2, 0,
        ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17,
        (SELECT file_external FROM nodes
          WHERE wc_id = ?1
            AND local_relpath = ?2
            AND op_depth = 0))

-- STMT_INSTALL_WORKING_NODE_FOR_DELETE
INSERT INTO nodes (
    wc_id, local_relpath, op_depth,
    parent_relpath, presence, kind)
VALUES(?1, ?2, ?3, ?4, MAP_BASE_DELETED, ?5)

-- STMT_REPLACE_WITH_BASE_DELETED
INSERT OR REPLACE INTO nodes (wc_id, local_relpath, op_depth, parent_relpath,
                              kind, moved_to, presence)
SELECT wc_id, local_relpath, op_depth, parent_relpath,
       kind, moved_to, MAP_BASE_DELETED
  FROM nodes
 WHERE wc_id = ?1
   AND local_relpath = ?2
   AND op_depth = ?3

/* If this query is updated, STMT_INSERT_DELETE_LIST should too.
   Use UNION ALL instead of a simple 'OR' to avoid creating a temp table */
-- STMT_INSERT_DELETE_FROM_NODE_RECURSIVE
INSERT INTO nodes (
    wc_id, local_relpath, op_depth, parent_relpath, presence, kind)
SELECT wc_id, local_relpath, ?4 /*op_depth*/, parent_relpath, MAP_BASE_DELETED,
       kind
FROM nodes
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = ?3
UNION ALL
SELECT wc_id, local_relpath, ?4 /*op_depth*/, parent_relpath, MAP_BASE_DELETED,
       kind
FROM nodes
WHERE wc_id = ?1
  AND IS_STRICT_DESCENDANT_OF(local_relpath, ?2)
  AND op_depth = ?3
  AND presence NOT IN (MAP_BASE_DELETED, MAP_NOT_PRESENT, MAP_EXCLUDED, MAP_SERVER_EXCLUDED)
  AND file_external IS NULL
ORDER BY local_relpath

-- STMT_INSERT_WORKING_NODE_FROM_BASE_COPY
INSERT OR REPLACE INTO nodes (
    wc_id, local_relpath, op_depth, parent_relpath, repos_id, repos_path,
    revision, presence, depth, kind, changed_revision, changed_date,
    changed_author, checksum, properties, translated_size, last_mod_time,
    symlink_target, moved_to )
SELECT wc_id, local_relpath, ?3 /*op_depth*/, parent_relpath, repos_id,
    repos_path, revision, presence, depth, kind, changed_revision,
    changed_date, changed_author, checksum, properties, translated_size,
    last_mod_time, symlink_target,
    (SELECT moved_to FROM nodes
     WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = ?3) moved_to
FROM nodes
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = 0

-- STMT_INSERT_DELETE_FROM_BASE
INSERT INTO nodes (
    wc_id, local_relpath, op_depth, parent_relpath, presence, kind)
SELECT wc_id, local_relpath, ?3 /*op_depth*/, parent_relpath,
    MAP_BASE_DELETED, kind
FROM nodes
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = 0

/* Not valid on the wc-root */
-- STMT_UPDATE_OP_DEPTH_INCREASE_RECURSIVE
UPDATE nodes SET op_depth = ?3 + 1
WHERE wc_id = ?1
 AND IS_STRICT_DESCENDANT_OF(local_relpath, ?2)
 AND op_depth = ?3

/* Duplicated SELECT body to avoid creating temporary table */
-- STMT_COPY_OP_DEPTH_RECURSIVE
INSERT INTO nodes (
    wc_id, local_relpath, op_depth, parent_relpath, repos_id, repos_path,
    revision, presence, depth, kind, changed_revision, changed_date,
    changed_author, checksum, properties, translated_size, last_mod_time,
    symlink_target, moved_here, moved_to )
SELECT
    wc_id, local_relpath, ?4, parent_relpath, repos_id,
    repos_path, revision, presence, depth, kind, changed_revision,
    changed_date, changed_author, checksum, properties, translated_size,
    last_mod_time, symlink_target, NULL, NULL
FROM nodes
WHERE wc_id = ?1 AND op_depth = ?3 AND local_relpath = ?2
UNION ALL
SELECT
    wc_id, local_relpath, ?4, parent_relpath, repos_id,
    repos_path, revision, presence, depth, kind, changed_revision,
    changed_date, changed_author, checksum, properties, translated_size,
    last_mod_time, symlink_target, NULL, NULL
FROM nodes
WHERE wc_id = ?1 AND op_depth = ?3
  AND IS_STRICT_DESCENDANT_OF(local_relpath, ?2)
ORDER BY local_relpath

-- STMT_DOES_NODE_EXIST
SELECT 1 FROM nodes WHERE wc_id = ?1 AND local_relpath = ?2
LIMIT 1

-- STMT_HAS_SERVER_EXCLUDED_DESCENDANTS
SELECT local_relpath FROM nodes
WHERE wc_id = ?1
  AND IS_STRICT_DESCENDANT_OF(local_relpath, ?2)
  AND op_depth = 0 AND presence = MAP_SERVER_EXCLUDED
LIMIT 1

/* Select all excluded nodes. Not valid on the WC-root */
-- STMT_SELECT_ALL_EXCLUDED_DESCENDANTS
SELECT local_relpath FROM nodes
WHERE wc_id = ?1
  AND IS_STRICT_DESCENDANT_OF(local_relpath, ?2)
  AND op_depth = 0
  AND (presence = MAP_SERVER_EXCLUDED OR presence = MAP_EXCLUDED)

/* Creates a copy from one top level NODE to a different location */
-- STMT_INSERT_WORKING_NODE_COPY_FROM
INSERT OR REPLACE INTO nodes (
    wc_id, local_relpath, op_depth, parent_relpath, repos_id,
    repos_path, revision, presence, depth, moved_here, kind, changed_revision,
    changed_date, changed_author, checksum, properties, translated_size,
    last_mod_time, symlink_target, moved_to )
SELECT wc_id, ?3 /*local_relpath*/, ?4 /*op_depth*/, ?5 /*parent_relpath*/,
    repos_id, repos_path, revision, ?6 /*presence*/, depth,
    ?7/*moved_here*/, kind, changed_revision, changed_date,
    changed_author, checksum, properties, translated_size,
    last_mod_time, symlink_target,
    (SELECT dst.moved_to FROM nodes AS dst
                         WHERE dst.wc_id = ?1
                         AND dst.local_relpath = ?3
                         AND dst.op_depth = ?4)
FROM nodes_current
WHERE wc_id = ?1 AND local_relpath = ?2

-- STMT_INSERT_WORKING_NODE_COPY_FROM_DEPTH
INSERT OR REPLACE INTO nodes (
    wc_id, local_relpath, op_depth, parent_relpath, repos_id,
    repos_path, revision, presence, depth, moved_here, kind, changed_revision,
    changed_date, changed_author, checksum, properties, translated_size,
    last_mod_time, symlink_target, moved_to )
SELECT wc_id, ?3 /*local_relpath*/, ?4 /*op_depth*/, ?5 /*parent_relpath*/,
    repos_id, repos_path, revision, ?6 /*presence*/, depth,
    ?8 /*moved_here*/, kind, changed_revision, changed_date,
    changed_author, checksum, properties, translated_size,
    last_mod_time, symlink_target,
    (SELECT dst.moved_to FROM nodes AS dst
                         WHERE dst.wc_id = ?1
                         AND dst.local_relpath = ?3
                         AND dst.op_depth = ?4)
FROM nodes
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = ?7

-- STMT_UPDATE_BASE_REVISION
UPDATE nodes SET revision = ?3
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = 0

-- STMT_UPDATE_BASE_REPOS
UPDATE nodes SET repos_id = ?3, repos_path = ?4
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = 0

-- STMT_ACTUAL_HAS_CHILDREN
SELECT 1 FROM actual_node
WHERE wc_id = ?1 AND parent_relpath = ?2
LIMIT 1

-- STMT_INSERT_EXTERNAL
INSERT OR REPLACE INTO externals (
    wc_id, local_relpath, parent_relpath, presence, kind, def_local_relpath,
    repos_id, def_repos_relpath, def_operational_revision, def_revision)
VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10)

-- STMT_SELECT_EXTERNAL_INFO
SELECT presence, kind, def_local_relpath, repos_id,
    def_repos_relpath, def_operational_revision, def_revision
FROM externals WHERE wc_id = ?1 AND local_relpath = ?2
LIMIT 1

-- STMT_DELETE_FILE_EXTERNALS
DELETE FROM nodes
WHERE wc_id = ?1
  AND IS_STRICT_DESCENDANT_OF(local_relpath, ?2)
  AND op_depth = 0
  AND file_external IS NOT NULL

-- STMT_DELETE_FILE_EXTERNAL_REGISTATIONS
DELETE FROM externals
WHERE wc_id = ?1
  AND IS_STRICT_DESCENDANT_OF(local_relpath, ?2)
  AND kind != MAP_DIR

-- STMT_DELETE_EXTERNAL_REGISTATIONS
DELETE FROM externals
WHERE wc_id = ?1
  AND IS_STRICT_DESCENDANT_OF(local_relpath, ?2)

/* Select all committable externals, i.e. only unpegged ones on the same
 * repository as the target path ?2, that are defined by WC ?1 to
 * live below the target path. It does not matter which ancestor has the
 * svn:externals definition, only the local path at which the external is
 * supposed to be checked out is queried.
 * Arguments:
 *  ?1: wc_id.
 *  ?2: the target path, local relpath inside ?1.
 *
 * ### NOTE: This statement deliberately removes file externals that live
 * inside an unversioned dir, because commit still breaks on those.
 * Once that's been fixed, the conditions below "--->8---" become obsolete. */
-- STMT_SELECT_COMMITTABLE_EXTERNALS_BELOW
SELECT local_relpath, kind, def_repos_relpath,
  (SELECT root FROM repository AS r WHERE r.id = e.repos_id)
FROM externals e
WHERE wc_id = ?1
  AND IS_STRICT_DESCENDANT_OF(local_relpath, ?2)
  AND def_revision IS NULL
  AND repos_id = (SELECT repos_id
                  FROM nodes AS n
                  WHERE n.wc_id = ?1
                    AND n.local_relpath = ''
                    AND n.op_depth = 0)
  AND ((kind='dir')
       OR EXISTS (SELECT 1 FROM nodes
                  WHERE nodes.wc_id = e.wc_id
                  AND nodes.local_relpath = e.parent_relpath))

-- STMT_SELECT_COMMITTABLE_EXTERNALS_IMMEDIATELY_BELOW
SELECT local_relpath, kind, def_repos_relpath,
  (SELECT root FROM repository AS r WHERE r.id = e.repos_id)
FROM externals e
WHERE wc_id = ?1
  AND IS_STRICT_DESCENDANT_OF(e.local_relpath, ?2)
  AND parent_relpath = ?2
  AND def_revision IS NULL
  AND repos_id = (SELECT repos_id
                    FROM nodes AS n
                    WHERE n.wc_id = ?1
                      AND n.local_relpath = ''
                      AND n.op_depth = 0)
  AND ((kind='dir')
       OR EXISTS (SELECT 1 FROM nodes
                  WHERE nodes.wc_id = e.wc_id
                  AND nodes.local_relpath = e.parent_relpath))

-- STMT_SELECT_EXTERNALS_DEFINED
SELECT local_relpath, def_local_relpath
FROM externals
/* ### The Sqlite optimizer needs help here ###
 * WHERE wc_id = ?1
 *   AND (def_local_relpath = ?2
 *        OR IS_STRICT_DESCENDANT_OF(def_local_relpath, ?2)) */
WHERE (wc_id = ?1 AND def_local_relpath = ?2)
   OR (wc_id = ?1 AND IS_STRICT_DESCENDANT_OF(def_local_relpath, ?2))

-- STMT_DELETE_EXTERNAL
DELETE FROM externals
WHERE wc_id = ?1 AND local_relpath = ?2

-- STMT_SELECT_EXTERNAL_PROPERTIES
/* ### It would be nice if Sqlite would handle
 * SELECT IFNULL((SELECT properties FROM actual_node a
 *                WHERE a.wc_id = ?1 AND A.local_relpath = n.local_relpath),
 *               properties),
 *        local_relpath, depth
 * FROM nodes_current n
 * WHERE wc_id = ?1
 *   AND (local_relpath = ?2
 *        OR IS_STRICT_DESCENDANT_OF(local_relpath, ?2))
 *   AND kind = MAP_DIR AND presence IN (MAP_NORMAL, MAP_INCOMPLETE)
 * ### But it would take a double table scan execution plan for it.
 * ### Maybe there is something else going on? */
SELECT IFNULL((SELECT properties FROM actual_node a
               WHERE a.wc_id = ?1 AND A.local_relpath = n.local_relpath),
              properties),
       local_relpath, depth
FROM nodes_current n
WHERE wc_id = ?1 AND local_relpath = ?2
  AND kind = MAP_DIR AND presence IN (MAP_NORMAL, MAP_INCOMPLETE)
UNION ALL
SELECT IFNULL((SELECT properties FROM actual_node a
               WHERE a.wc_id = ?1 AND A.local_relpath = n.local_relpath),
              properties),
       local_relpath, depth
FROM nodes_current n
WHERE wc_id = ?1 AND IS_STRICT_DESCENDANT_OF(local_relpath, ?2)
  AND kind = MAP_DIR AND presence IN (MAP_NORMAL, MAP_INCOMPLETE)

-- STMT_SELECT_CURRENT_PROPS_RECURSIVE
/* ### Ugly OR to make sqlite use the proper optimizations */
SELECT IFNULL((SELECT properties FROM actual_node a
               WHERE a.wc_id = ?1 AND A.local_relpath = n.local_relpath),
              properties),
       local_relpath
FROM nodes_current n
WHERE (wc_id = ?1 AND local_relpath = ?2)
   OR (wc_id = ?1 AND IS_STRICT_DESCENDANT_OF(local_relpath, ?2))

-- STMT_PRAGMA_LOCKING_MODE
PRAGMA locking_mode = exclusive;
/* Testing shows DELETE is faster than TRUNCATE on NFS and
   exclusive-locking is mostly used on remote file systems. */
PRAGMA journal_mode = DELETE

-- STMT_FIND_REPOS_PATH_IN_WC
SELECT local_relpath FROM nodes_current
  WHERE wc_id = ?1 AND repos_path = ?2

/* ------------------------------------------------------------------------- */

/* these are used in entries.c  */

-- STMT_INSERT_ACTUAL_NODE
INSERT OR REPLACE INTO actual_node (
  wc_id, local_relpath, parent_relpath, properties, changelist, conflict_data)
VALUES (?1, ?2, ?3, ?4, ?5, ?6)

/* ------------------------------------------------------------------------- */

/* these are used in upgrade.c  */

-- STMT_SELECT_ALL_FILES
SELECT local_relpath FROM nodes_current
WHERE wc_id = ?1 AND parent_relpath = ?2 AND kind = MAP_FILE

-- STMT_UPDATE_NODE_PROPS
UPDATE nodes SET properties = ?4
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = ?3

-- STMT_PRAGMA_TABLE_INFO_NODES
PRAGMA table_info("NODES")

/* --------------------------------------------------------------------------
 * Complex queries for callback walks, caching results in a temporary table.
 *
 * These target table are then used for joins against NODES, or for reporting
 */

-- STMT_CREATE_TARGET_PROP_CACHE
DROP TABLE IF EXISTS target_prop_cache;
CREATE TEMPORARY TABLE target_prop_cache (
  local_relpath TEXT NOT NULL PRIMARY KEY,
  kind TEXT NOT NULL,
  properties BLOB
);
/* ###  Need index?
CREATE UNIQUE INDEX temp__node_props_cache_unique
  ON temp__node_props_cache (local_relpath) */

-- STMT_CACHE_TARGET_PROPS
INSERT INTO target_prop_cache(local_relpath, kind, properties)
 SELECT n.local_relpath, n.kind,
        IFNULL((SELECT properties FROM actual_node AS a
                 WHERE a.wc_id = n.wc_id
                   AND a.local_relpath = n.local_relpath),
               n.properties)
   FROM targets_list AS t
   JOIN nodes AS n
     ON n.wc_id = ?1
    AND n.local_relpath = t.local_relpath
    AND n.op_depth = (SELECT MAX(op_depth) FROM nodes AS n3
                      WHERE n3.wc_id = ?1
                        AND n3.local_relpath = t.local_relpath)
  WHERE t.wc_id = ?1
    AND (presence=MAP_NORMAL OR presence=MAP_INCOMPLETE)
  ORDER BY t.local_relpath

-- STMT_CACHE_TARGET_PRISTINE_PROPS
INSERT INTO target_prop_cache(local_relpath, kind, properties)
 SELECT n.local_relpath, n.kind,
        CASE n.presence
          WHEN MAP_BASE_DELETED
          THEN (SELECT properties FROM nodes AS p
                 WHERE p.wc_id = n.wc_id
                   AND p.local_relpath = n.local_relpath
                   AND p.op_depth < n.op_depth
                 ORDER BY p.op_depth DESC /* LIMIT 1 */)
          ELSE properties END
  FROM targets_list AS t
  JOIN nodes AS n
    ON n.wc_id = ?1
   AND n.local_relpath = t.local_relpath
   AND n.op_depth = (SELECT MAX(op_depth) FROM nodes AS n3
                     WHERE n3.wc_id = ?1
                       AND n3.local_relpath = t.local_relpath)
  WHERE t.wc_id = ?1
    AND (presence = MAP_NORMAL
         OR presence = MAP_INCOMPLETE
         OR presence = MAP_BASE_DELETED)
  ORDER BY t.local_relpath

-- STMT_SELECT_ALL_TARGET_PROP_CACHE
SELECT local_relpath, properties FROM target_prop_cache
ORDER BY local_relpath

-- STMT_DROP_TARGET_PROP_CACHE
DROP TABLE target_prop_cache;

-- STMT_CREATE_REVERT_LIST
DROP TABLE IF EXISTS revert_list;
CREATE TEMPORARY TABLE revert_list (
   /* need wc_id if/when revert spans multiple working copies */
   local_relpath TEXT NOT NULL,
   actual INTEGER NOT NULL,         /* 1 if an actual row, 0 if a nodes row */
   conflict_data BLOB,
   notify INTEGER,         /* 1 if an actual row had props or tree conflict */
   op_depth INTEGER,
   repos_id INTEGER,
   kind TEXT,
   PRIMARY KEY (local_relpath, actual)
   );
DROP TRIGGER IF EXISTS   trigger_revert_list_nodes;
CREATE TEMPORARY TRIGGER trigger_revert_list_nodes
BEFORE DELETE ON nodes
BEGIN
   INSERT OR REPLACE INTO revert_list(local_relpath, actual, op_depth,
                                      repos_id, kind)
   SELECT OLD.local_relpath, 0, OLD.op_depth, OLD.repos_id, OLD.kind;
END;
DROP TRIGGER IF EXISTS   trigger_revert_list_actual_delete;
CREATE TEMPORARY TRIGGER trigger_revert_list_actual_delete
BEFORE DELETE ON actual_node
BEGIN
   INSERT OR REPLACE INTO revert_list(local_relpath, actual, conflict_data,
                                      notify)
   SELECT OLD.local_relpath, 1, OLD.conflict_data,
          CASE
            WHEN OLD.properties IS NOT NULL
            THEN 1
            WHEN NOT EXISTS(SELECT 1 FROM NODES n
                            WHERE n.wc_id = OLD.wc_id
                              AND n.local_relpath = OLD.local_relpath)
            THEN 1
          END notify
   WHERE OLD.conflict_data IS NOT NULL
      OR notify IS NOT NULL;
END;
DROP TRIGGER IF EXISTS   trigger_revert_list_actual_update;
CREATE TEMPORARY TRIGGER trigger_revert_list_actual_update
BEFORE UPDATE ON actual_node
BEGIN
   INSERT OR REPLACE INTO revert_list(local_relpath, actual, conflict_data,
                                      notify)
   SELECT OLD.local_relpath, 1, OLD.conflict_data,
          CASE
            WHEN OLD.properties IS NOT NULL
            THEN 1
            WHEN NOT EXISTS(SELECT 1 FROM NODES n
                            WHERE n.wc_id = OLD.wc_id
                              AND n.local_relpath = OLD.local_relpath)
            THEN 1
          END notify
   WHERE OLD.conflict_data IS NOT NULL
      OR notify IS NOT NULL;
END

-- STMT_DROP_REVERT_LIST_TRIGGERS
DROP TRIGGER trigger_revert_list_nodes;
DROP TRIGGER trigger_revert_list_actual_delete;
DROP TRIGGER trigger_revert_list_actual_update

-- STMT_SELECT_REVERT_LIST
SELECT actual, notify, kind, op_depth, repos_id, conflict_data
FROM revert_list
WHERE local_relpath = ?1
ORDER BY actual DESC

-- STMT_SELECT_REVERT_LIST_COPIED_CHILDREN
SELECT local_relpath, kind
FROM revert_list
WHERE IS_STRICT_DESCENDANT_OF(local_relpath, ?1)
  AND op_depth >= ?2
  AND repos_id IS NOT NULL
ORDER BY local_relpath

-- STMT_DELETE_REVERT_LIST
DELETE FROM revert_list WHERE local_relpath = ?1

-- STMT_SELECT_REVERT_LIST_RECURSIVE
SELECT p.local_relpath, n.kind, a.notify, a.kind
FROM (SELECT DISTINCT local_relpath
      FROM revert_list
      WHERE (local_relpath = ?1
        OR IS_STRICT_DESCENDANT_OF(local_relpath, ?1))) p

LEFT JOIN revert_list n ON n.local_relpath=p.local_relpath AND n.actual=0
LEFT JOIN revert_list a ON a.local_relpath=p.local_relpath AND a.actual=1
ORDER BY p.local_relpath

-- STMT_DELETE_REVERT_LIST_RECURSIVE
DELETE FROM revert_list
WHERE (local_relpath = ?1
       OR IS_STRICT_DESCENDANT_OF(local_relpath, ?1))

-- STMT_DROP_REVERT_LIST
DROP TABLE IF EXISTS revert_list

-- STMT_CREATE_DELETE_LIST
DROP TABLE IF EXISTS delete_list;
CREATE TEMPORARY TABLE delete_list (
/* ### we should put the wc_id in here in case a delete spans multiple
   ### working copies. queries, etc will need to be adjusted.  */
   local_relpath TEXT PRIMARY KEY NOT NULL UNIQUE
   )

/* This matches the selection in STMT_INSERT_DELETE_FROM_NODE_RECURSIVE.
   A subquery is used instead of nodes_current to avoid a table scan */
-- STMT_INSERT_DELETE_LIST
INSERT INTO delete_list(local_relpath)
SELECT ?2
UNION ALL
SELECT local_relpath FROM nodes AS n
WHERE wc_id = ?1
  AND IS_STRICT_DESCENDANT_OF(local_relpath, ?2)
  AND op_depth >= ?3
  AND op_depth = (SELECT MAX(s.op_depth) FROM nodes AS s
                  WHERE s.wc_id = ?1
                    AND s.local_relpath = n.local_relpath)
  AND presence NOT IN (MAP_BASE_DELETED, MAP_NOT_PRESENT, MAP_EXCLUDED, MAP_SERVER_EXCLUDED)
  AND file_external IS NULL
ORDER by local_relpath

-- STMT_SELECT_DELETE_LIST
SELECT local_relpath FROM delete_list
ORDER BY local_relpath

-- STMT_FINALIZE_DELETE
DROP TABLE IF EXISTS delete_list

-- STMT_CREATE_UPDATE_MOVE_LIST
DROP TABLE IF EXISTS update_move_list;
CREATE TEMPORARY TABLE update_move_list (
/* ### we should put the wc_id in here in case a move update spans multiple
   ### working copies. queries, etc will need to be adjusted.  */
  local_relpath TEXT PRIMARY KEY NOT NULL UNIQUE,
  action INTEGER NOT NULL,
  kind TEXT NOT NULL,
  content_state INTEGER NOT NULL,
  prop_state  INTEGER NOT NULL
  )

-- STMT_INSERT_UPDATE_MOVE_LIST
INSERT INTO update_move_list(local_relpath, action, kind, content_state,
  prop_state)
VALUES (?1, ?2, ?3, ?4, ?5)

-- STMT_SELECT_UPDATE_MOVE_LIST
SELECT local_relpath, action, kind, content_state, prop_state
FROM update_move_list
ORDER BY local_relpath

-- STMT_FINALIZE_UPDATE_MOVE
DROP TABLE IF EXISTS update_move_list

-- STMT_MOVE_NOTIFY_TO_REVERT
INSERT INTO revert_list (local_relpath, notify, kind, actual)
       SELECT local_relpath, 2, kind, 1 FROM update_move_list;
DROP TABLE update_move_list

/* ------------------------------------------------------------------------- */

/* Queries for revision status. */

-- STMT_SELECT_MIN_MAX_REVISIONS
SELECT MIN(revision), MAX(revision),
       MIN(changed_revision), MAX(changed_revision) FROM nodes
  WHERE wc_id = ?1
    AND (local_relpath = ?2
         OR IS_STRICT_DESCENDANT_OF(local_relpath, ?2))
    AND presence IN (MAP_NORMAL, MAP_INCOMPLETE)
    AND file_external IS NULL
    AND op_depth = 0

-- STMT_HAS_SPARSE_NODES
SELECT 1 FROM nodes
WHERE wc_id = ?1
  AND (local_relpath = ?2
       OR IS_STRICT_DESCENDANT_OF(local_relpath, ?2))
  AND op_depth = 0
  AND (presence IN (MAP_SERVER_EXCLUDED, MAP_EXCLUDED)
        OR depth NOT IN (MAP_DEPTH_INFINITY, MAP_DEPTH_UNKNOWN))
  AND file_external IS NULL
LIMIT 1

-- STMT_SUBTREE_HAS_TREE_MODIFICATIONS
SELECT 1 FROM nodes
WHERE wc_id = ?1
  AND (local_relpath = ?2
       OR IS_STRICT_DESCENDANT_OF(local_relpath, ?2))
  AND op_depth > 0
LIMIT 1

-- STMT_SUBTREE_HAS_PROP_MODIFICATIONS
SELECT 1 FROM actual_node
WHERE wc_id = ?1
  AND (local_relpath = ?2
       OR IS_STRICT_DESCENDANT_OF(local_relpath, ?2))
  AND properties IS NOT NULL
LIMIT 1

-- STMT_HAS_SWITCHED
SELECT 1
FROM nodes
WHERE wc_id = ?1
  AND IS_STRICT_DESCENDANT_OF(local_relpath, ?2)
  AND op_depth = 0
  AND file_external IS NULL
  AND presence IN (MAP_NORMAL, MAP_INCOMPLETE)
  AND repos_path IS NOT RELPATH_SKIP_JOIN(?2, ?3, local_relpath)
LIMIT 1

-- STMT_SELECT_MOVED_FROM_RELPATH
SELECT local_relpath, op_depth FROM nodes
WHERE wc_id = ?1 AND moved_to = ?2 AND op_depth > 0

-- STMT_UPDATE_MOVED_TO_RELPATH
UPDATE nodes SET moved_to = ?4
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = ?3

-- STMT_CLEAR_MOVED_TO_RELPATH
UPDATE nodes SET moved_to = NULL
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth = ?3

-- STMT_CLEAR_MOVED_HERE_RECURSIVE
UPDATE nodes SET moved_here = NULL
WHERE wc_id = ?1
 AND (local_relpath = ?2 OR IS_STRICT_DESCENDANT_OF(local_relpath, ?2))
 AND op_depth = ?3

/* This statement returns pairs of move-roots below the path ?2 in WC_ID ?1.
 * Each row returns a moved-here path (always a child of ?2) in the first
 * column, and its matching moved-away (deleted) path in the second column. */
-- STMT_SELECT_MOVED_HERE_CHILDREN
SELECT moved_to, local_relpath FROM nodes
WHERE wc_id = ?1 AND op_depth > 0
  AND IS_STRICT_DESCENDANT_OF(moved_to, ?2)

/* If the node is moved here (r.moved_here = 1) we are really interested in
   where the node was moved from. To obtain that we need the op_depth, but
   this form of select only allows a single return value */
-- STMT_SELECT_MOVED_FOR_DELETE
SELECT local_relpath, moved_to, op_depth,
       (SELECT CASE WHEN r.moved_here THEN r.op_depth END FROM nodes r
        WHERE r.wc_id = ?1
          AND r.local_relpath = n.local_relpath
          AND r.op_depth < n.op_depth
        ORDER BY r.op_depth DESC LIMIT 1) AS moved_here_op_depth
 FROM nodes n
WHERE wc_id = ?1
  AND (local_relpath = ?2 OR IS_STRICT_DESCENDANT_OF(local_relpath, ?2))
  AND moved_to IS NOT NULL
  AND op_depth >= ?3

-- STMT_SELECT_MOVED_FROM_FOR_DELETE
SELECT local_relpath, op_depth,
       (SELECT CASE WHEN r.moved_here THEN r.op_depth END FROM nodes r
        WHERE r.wc_id = ?1
          AND r.local_relpath = n.local_relpath
          AND r.op_depth < n.op_depth
        ORDER BY r.op_depth DESC LIMIT 1) AS moved_here_op_depth
 FROM nodes n
WHERE wc_id = ?1 AND moved_to = ?2 AND op_depth > 0

-- STMT_UPDATE_MOVED_TO_DESCENDANTS
UPDATE nodes SET moved_to = RELPATH_SKIP_JOIN(?2, ?3, moved_to)
 WHERE wc_id = ?1
   AND IS_STRICT_DESCENDANT_OF(moved_to, ?2)

-- STMT_CLEAR_MOVED_TO_DESCENDANTS
UPDATE nodes SET moved_to = NULL
 WHERE wc_id = ?1
   AND IS_STRICT_DESCENDANT_OF(moved_to, ?2)

-- STMT_SELECT_MOVED_PAIR3
SELECT n.local_relpath, d.moved_to, d.op_depth, n.kind
FROM nodes n
JOIN nodes d ON d.wc_id = ?1 AND d.local_relpath = n.local_relpath
 AND d.op_depth = (SELECT MIN(dd.op_depth)
                    FROM nodes dd
                    WHERE dd.wc_id = ?1
                      AND dd.local_relpath = d.local_relpath
                      AND dd.op_depth > ?3)
WHERE n.wc_id = ?1 AND n.local_relpath = ?2 AND n.op_depth = ?3
  AND d.moved_to IS NOT NULL
UNION ALL
SELECT n.local_relpath, d.moved_to, d.op_depth, n.kind
FROM nodes n
JOIN nodes d ON d.wc_id = ?1 AND d.local_relpath = n.local_relpath
 AND d.op_depth = (SELECT MIN(dd.op_depth)
                    FROM nodes dd
                    WHERE dd.wc_id = ?1
                      AND dd.local_relpath = d.local_relpath
                      AND dd.op_depth > ?3)
WHERE n.wc_id = ?1 AND IS_STRICT_DESCENDANT_OF(n.local_relpath, ?2)
  AND n.op_depth = ?3
  AND d.moved_to IS NOT NULL
ORDER BY n.local_relpath

-- STMT_SELECT_MOVED_OUTSIDE
SELECT local_relpath, moved_to, op_depth FROM nodes
WHERE wc_id = ?1
  AND (local_relpath = ?2 OR IS_STRICT_DESCENDANT_OF(local_relpath, ?2))
  AND op_depth >= ?3
  AND moved_to IS NOT NULL
  AND NOT IS_STRICT_DESCENDANT_OF(moved_to, ?2)

-- STMT_SELECT_MOVED_DESCENDANTS_SRC
SELECT s.op_depth, n.local_relpath, n.kind, n.repos_path, s.moved_to
FROM nodes n
JOIN nodes s ON s.wc_id = n.wc_id AND s.local_relpath = n.local_relpath
 AND s.op_depth = (SELECT MIN(d.op_depth)
                    FROM nodes d
                    WHERE d.wc_id = ?1
                      AND d.local_relpath = s.local_relpath
                      AND d.op_depth > ?3)
WHERE n.wc_id = ?1 AND n.op_depth = ?3
  AND (n.local_relpath = ?2 OR IS_STRICT_DESCENDANT_OF(n.local_relpath, ?2))
  AND s.moved_to IS NOT NULL

-- STMT_COMMIT_UPDATE_ORIGIN
UPDATE nodes SET repos_id = ?4,
                 repos_path = RELPATH_SKIP_JOIN(?2, ?5, local_relpath),
                 revision = ?6
WHERE wc_id = ?1
  AND (local_relpath = ?2
       OR IS_STRICT_DESCENDANT_OF(local_relpath, ?2))
  AND op_depth = ?3

-- STMT_HAS_LAYER_BETWEEN
SELECT 1 FROM NODES
WHERE wc_id = ?1 AND local_relpath = ?2 AND op_depth > ?3 AND op_depth < ?4

-- STMT_SELECT_REPOS_PATH_REVISION
SELECT local_relpath, repos_path, revision FROM nodes
WHERE wc_id = ?1
  AND IS_STRICT_DESCENDANT_OF(local_relpath, ?2)
  AND op_depth = 0
ORDER BY local_relpath

-- STMT_SELECT_HAS_NON_FILE_CHILDREN
SELECT 1 FROM nodes
WHERE wc_id = ?1 AND parent_relpath = ?2 AND op_depth = ?3 AND kind != MAP_FILE
LIMIT 1

-- STMT_SELECT_HAS_GRANDCHILDREN
SELECT 1 FROM nodes
WHERE wc_id = ?1
  AND IS_STRICT_DESCENDANT_OF(parent_relpath, ?2)
  AND op_depth = ?3
  AND file_external IS NULL
LIMIT 1

/* ------------------------------------------------------------------------- */

/* Queries for verification. */

-- STMT_SELECT_ALL_NODES
SELECT op_depth, local_relpath, parent_relpath, file_external FROM nodes
WHERE wc_id = ?1

/* ------------------------------------------------------------------------- */

/* Queries for cached inherited properties. */

/* Update the inherited properties of a single base node. */
-- STMT_UPDATE_IPROP
UPDATE nodes
SET inherited_props = ?3
WHERE (wc_id = ?1 AND local_relpath = ?2 AND op_depth = 0)

/* Select a single path if its base node has cached inherited properties. */
-- STMT_SELECT_IPROPS_NODE
SELECT local_relpath, repos_path FROM nodes
WHERE wc_id = ?1
  AND local_relpath = ?2
  AND op_depth = 0
  AND (inherited_props not null)

/* Select all paths whose base nodes are below a given path, which
   have cached inherited properties. */
-- STMT_SELECT_IPROPS_RECURSIVE
SELECT local_relpath, repos_path FROM nodes
WHERE wc_id = ?1
  AND IS_STRICT_DESCENDANT_OF(local_relpath, ?2)
  AND op_depth = 0
  AND (inherited_props not null)

-- STMT_SELECT_IPROPS_CHILDREN
SELECT local_relpath, repos_path FROM nodes
WHERE wc_id = ?1
  AND parent_relpath = ?2
  AND op_depth = 0
  AND (inherited_props not null)

-- STMT_HAVE_STAT1_TABLE
SELECT 1 FROM sqlite_master WHERE name='sqlite_stat1' AND type='table'
LIMIT 1

/* ------------------------------------------------------------------------- */

/* Grab all the statements related to the schema.  */

-- include: wc-metadata
-- include: wc-checks
