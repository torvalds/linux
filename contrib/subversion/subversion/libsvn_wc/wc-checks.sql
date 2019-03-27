/* wc-checks.sql -- trigger-based checks for the wc-metadata database.
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


-- STMT_VERIFICATION_TRIGGERS

/* ------------------------------------------------------------------------- */

CREATE TEMPORARY TRIGGER no_repository_updates BEFORE UPDATE ON repository
BEGIN
  SELECT RAISE(FAIL, 'Updates to REPOSITORY are not allowed.');
END;

/* ------------------------------------------------------------------------- */

/* Verify: on every NODES row: parent_relpath is parent of local_relpath */
CREATE TEMPORARY TRIGGER validation_01 BEFORE INSERT ON nodes
WHEN NOT ((new.local_relpath = '' AND new.parent_relpath IS NULL)
          OR (relpath_depth(new.local_relpath)
              = relpath_depth(new.parent_relpath) + 1))
BEGIN
  SELECT RAISE(FAIL, 'WC DB validity check 01 failed');
END;

/* Verify: on every NODES row: its op-depth <= its own depth */
CREATE TEMPORARY TRIGGER validation_02 BEFORE INSERT ON nodes
WHEN NOT new.op_depth <= relpath_depth(new.local_relpath)
BEGIN
  SELECT RAISE(FAIL, 'WC DB validity check 02 failed');
END;

/* Verify: on every NODES row: it is an op-root or it has a parent with the
    sames op-depth. (Except when the node is a file external) */
CREATE TEMPORARY TRIGGER validation_03 BEFORE INSERT ON nodes
WHEN NOT (
    (new.op_depth = relpath_depth(new.local_relpath))
    OR
    (EXISTS (SELECT 1 FROM nodes
              WHERE wc_id = new.wc_id AND op_depth = new.op_depth
                AND local_relpath = new.parent_relpath))
  )
 AND NOT (new.file_external IS NOT NULL AND new.op_depth = 0)
BEGIN
  SELECT RAISE(FAIL, 'WC DB validity check 03 failed');
END;

/* Verify: on every ACTUAL row (except root): a NODES row exists at its
 * parent path. */
CREATE TEMPORARY TRIGGER validation_04 BEFORE INSERT ON actual_node
WHEN NOT (new.local_relpath = ''
          OR EXISTS (SELECT 1 FROM nodes
                       WHERE wc_id = new.wc_id
                         AND local_relpath = new.parent_relpath))
BEGIN
  SELECT RAISE(FAIL, 'WC DB validity check 04 failed');
END;

-- STMT_STATIC_VERIFY
SELECT local_relpath, op_depth, 1, 'Invalid parent relpath set in NODES'
FROM nodes n WHERE local_relpath != ''
 AND (parent_relpath IS NULL
      OR NOT IS_STRICT_DESCENDANT_OF(local_relpath, parent_relpath)
      OR relpath_depth(local_relpath) != relpath_depth(parent_relpath)+1)

UNION ALL

SELECT local_relpath, -1, 2, 'Invalid parent relpath set in ACTUAL'
FROM actual_node a WHERE local_relpath != ''
 AND (parent_relpath IS NULL
      OR NOT IS_STRICT_DESCENDANT_OF(local_relpath, parent_relpath)
      OR relpath_depth(local_relpath) != relpath_depth(parent_relpath)+1)

UNION ALL

/* All ACTUAL nodes must have an equivalent NODE in NODES
   or be only one level deep (delete-delete tc) */
SELECT local_relpath, -1, 10, 'No ancestor in ACTUAL'
FROM actual_node a WHERE local_relpath != ''
 AND NOT EXISTS(SELECT 1 from nodes i
                WHERE i.wc_id=a.wc_id
                  AND i.local_relpath=a.parent_relpath)
 AND NOT EXISTS(SELECT 1 from nodes i
                WHERE i.wc_id=a.wc_id
                  AND i.local_relpath=a.local_relpath)

UNION ALL
/* Verify if the ACTUAL data makes sense for the related node.
   Only conflict data is valid if there is none */
SELECT a.local_relpath, -1, 11, 'Bad or Unneeded actual data'
FROM actual_node a
LEFT JOIN nodes n on n.wc_id = a.wc_id AND n.local_relpath = a.local_relpath
   AND n.op_depth = (SELECT MAX(op_depth) from nodes i
                     WHERE i.wc_id=a.wc_id AND i.local_relpath=a.local_relpath)
WHERE (a.properties IS NOT NULL
       AND (n.presence IS NULL
            OR n.presence NOT IN (MAP_NORMAL, MAP_INCOMPLETE)))
   OR (a.changelist IS NOT NULL AND (n.kind IS NOT NULL AND n.kind != MAP_FILE))
   OR (a.conflict_data IS NULL AND a.properties IS NULL AND a.changelist IS NULL)
 AND NOT EXISTS(SELECT 1 from nodes i
                WHERE i.wc_id=a.wc_id
                  AND i.local_relpath=a.parent_relpath)

UNION ALL

/* A parent node must exist for every normal node except the root.
   That node must exist at a lower or equal op-depth */
SELECT local_relpath, op_depth, 20, 'No ancestor in NODES'
FROM nodes n WHERE local_relpath != ''
 AND file_external IS NULL
 AND NOT EXISTS(SELECT 1 from nodes i
                WHERE i.wc_id=n.wc_id
                  AND i.local_relpath=n.parent_relpath
                  AND i.op_depth <= n.op_depth)

UNION ALL
/* If a node is not present in the working copy (normal, add, copy) it doesn't
   have revision details stored on this record */
SELECT local_relpath, op_depth, 21, 'Unneeded node data'
FROM nodes
WHERE presence NOT IN (MAP_NORMAL, MAP_INCOMPLETE)
AND (properties IS NOT NULL
     OR checksum IS NOT NULL
     OR depth IS NOT NULL
     OR symlink_target IS NOT NULL
     OR changed_revision IS NOT NULL
     OR (changed_date IS NOT NULL AND changed_date != 0)
     OR changed_author IS NOT NULL
     OR translated_size IS NOT NULL
     OR last_mod_time IS NOT NULL
     OR dav_cache IS NOT NULL
     OR file_external IS NOT NULL
     OR inherited_props IS NOT NULL)

UNION ALL
/* base-deleted nodes don't have a repository location. They are just
   shadowing without a replacement */
SELECT local_relpath, op_depth, 22, 'Unneeded base-deleted node data'
FROM nodes
WHERE presence IN (MAP_BASE_DELETED)
AND (repos_id IS NOT NULL
     OR repos_path IS NOT NULL
     OR revision IS NOT NULL)

UNION ALL
/* Verify if type specific data is set (or not set for wrong type) */
SELECT local_relpath, op_depth, 23, 'Kind specific data invalid on normal'
FROM nodes
WHERE presence IN (MAP_NORMAL, MAP_INCOMPLETE)
AND (kind IS NULL
     OR (repos_path IS NULL
         AND (properties IS NOT NULL
              OR changed_revision IS NOT NULL
              OR changed_author IS NOT NULL
              OR (changed_date IS NOT NULL AND changed_date != 0)))
     OR (CASE WHEN kind = MAP_FILE AND repos_path IS NOT NULL
                                   THEN checksum IS NULL
                                   ELSE checksum IS NOT NULL END)
     OR (CASE WHEN kind = MAP_DIR THEN depth IS NULL
                                  ELSE depth IS NOT NULL END)
     OR (CASE WHEN kind = MAP_SYMLINK THEN symlink_target IS NULL
                                      ELSE symlink_target IS NOT NULL END))

UNION ALL
/* Local-adds are always their own operation (read: they don't have
   op-depth descendants, nor are op-depth descendants */
SELECT local_relpath, op_depth, 24, 'Invalid op-depth for local add'
FROM nodes
WHERE presence IN (MAP_NORMAL, MAP_INCOMPLETE)
  AND repos_path IS NULL
  AND op_depth != relpath_depth(local_relpath)

UNION ALL
/* op-depth descendants are only valid if they have a direct parent
   node at the same op-depth. Only certain types allow further
   descendants */
SELECT local_relpath, op_depth, 25, 'Node missing op-depth ancestor'
FROM nodes n
WHERE op_depth < relpath_depth(local_relpath)
  AND file_external IS NULL
  AND NOT EXISTS(SELECT 1 FROM nodes p
                 WHERE p.wc_id=n.wc_id AND p.local_relpath=n.parent_relpath
                   AND p.op_depth=n.op_depth
                   AND (p.presence IN (MAP_NORMAL, MAP_INCOMPLETE)
                        OR (p.presence IN (MAP_BASE_DELETED, MAP_NOT_PRESENT)
                            AND n.presence = MAP_BASE_DELETED)))

UNION ALL
/* Present op-depth descendants have the repository location implied by their
   ancestor */
SELECT n.local_relpath, n.op_depth, 26, 'Copied descendant mismatch'
FROM nodes n
JOIN nodes p
  ON p.wc_id=n.wc_id AND p.local_relpath=n.parent_relpath
  AND n.op_depth=p.op_depth
WHERE n.op_depth > 0 AND n.presence IN (MAP_NORMAL, MAP_INCOMPLETE)
   AND (n.repos_id != p.repos_id
        OR n.repos_path !=
           RELPATH_SKIP_JOIN(n.parent_relpath, p.repos_path, n.local_relpath)
        OR n.revision != p.revision
        OR p.kind != MAP_DIR
        OR n.moved_here IS NOT p.moved_here)

UNION ALL
/* Only certain presence values are valid as op-root.
   Note that the wc-root always has presence normal or incomplete */
SELECT n.local_relpath, n.op_depth, 27, 'Invalid op-root presence'
FROM nodes n
WHERE n.op_depth = relpath_depth(local_relpath)
  AND presence NOT IN (MAP_NORMAL, MAP_INCOMPLETE, MAP_BASE_DELETED)

UNION ALL
/* If a node is shadowed, all its present op-depth descendants
   must be shadowed at the same op-depth as well */
SELECT n.local_relpath, s.op_depth, 28, 'Incomplete shadowing'
FROM nodes n
JOIN nodes s ON s.wc_id=n.wc_id AND s.local_relpath=n.local_relpath
 AND s.op_depth = relpath_depth(s.local_relpath)
 AND s.op_depth = (SELECT MIN(op_depth) FROM nodes d
                   WHERE d.wc_id=s.wc_id AND d.local_relpath=s.local_relpath
                     AND d.op_depth > n.op_depth)
WHERE n.presence IN (MAP_NORMAL, MAP_INCOMPLETE)
  AND EXISTS(SELECT 1
             FROM nodes dn
             WHERE dn.wc_id=n.wc_id AND dn.op_depth=n.op_depth
               AND dn.presence IN (MAP_NORMAL, MAP_INCOMPLETE)
               AND IS_STRICT_DESCENDANT_OF(dn.local_relpath, n.local_relpath)
               AND dn.file_external IS NULL
               AND NOT EXISTS(SELECT 1
                              FROM nodes ds
                              WHERE ds.wc_id=n.wc_id AND ds.op_depth=s.op_depth
                                AND ds.local_relpath=dn.local_relpath))

UNION ALL
/* A base-delete is only valid if it directly deletes a present node */
SELECT s.local_relpath, s.op_depth, 29, 'Invalid base-delete'
FROM nodes s
LEFT JOIN nodes n ON n.wc_id=s.wc_id AND n.local_relpath=s.local_relpath
 AND n.op_depth = (SELECT MAX(op_depth) FROM nodes d
                   WHERE d.wc_id=s.wc_id AND d.local_relpath=s.local_relpath
                     AND d.op_depth < s.op_depth)
WHERE s.presence = MAP_BASE_DELETED
  AND (n.presence IS NULL
       OR n.presence NOT IN (MAP_NORMAL, MAP_INCOMPLETE)
       /*OR n.kind != s.kind*/)

UNION ALL
/* Moves are stored in the working layers, not in BASE */
SELECT n.local_relpath, n.op_depth, 30, 'Invalid data for BASE'
FROM nodes n
WHERE n.op_depth = 0
  AND (n.moved_to IS NOT NULL
       OR n.moved_here IS NOT NULL)

UNION ALL
/* If moved_here is set on an op-root, there must be a proper moved_to */
SELECT d.local_relpath, d.op_depth, 60, 'Moved here without origin'
FROM nodes d
WHERE d.op_depth = relpath_depth(d.local_relpath)
  AND d.moved_here IS NOT NULL
  AND NOT EXISTS(SELECT 1 FROM nodes s
                 WHERE s.wc_id = d.wc_id AND s.moved_to = d.local_relpath)

UNION ALL
/* If moved_to is set there should be an moved op root at the target */
SELECT s.local_relpath, s.op_depth, 61, 'Moved to without target'
FROM nodes s
WHERE s.moved_to IS NOT NULL
  AND NOT EXISTS(SELECT 1 FROM nodes d
                 WHERE d.wc_id = s.wc_id AND d.local_relpath = s.moved_to
                   AND d.op_depth = relpath_depth(d.local_relpath)
                   AND d.moved_here =1 AND d.repos_path IS NOT NULL)
