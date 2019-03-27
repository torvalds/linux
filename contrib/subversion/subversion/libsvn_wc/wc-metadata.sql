/* wc-metadata.sql -- schema used in the wc-metadata SQLite database
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

/*
 * the KIND column in these tables has one of the following values
 * (documented in the corresponding C type #svn_kind_t):
 *   "file"
 *   "dir"
 *   "symlink"
 *   "unknown"
 *
 * the PRESENCE column in these tables has one of the following values
 * (see also the C type #svn_wc__db_status_t):
 *   "normal"
 *   "server-excluded" -- server has declared it excluded (ie. authz failure)
 *   "excluded" -- administratively excluded (ie. sparse WC)
 *   "not-present" -- node not present at this REV
 *   "incomplete" -- state hasn't been filled in
 *   "base-deleted" -- node represents a delete of a BASE node
 */

/* One big list of statements to create our (current) schema.  */
-- STMT_CREATE_SCHEMA

/* ------------------------------------------------------------------------- */

CREATE TABLE REPOSITORY (
  id INTEGER PRIMARY KEY AUTOINCREMENT,

  /* The root URL of the repository. This value is URI-encoded.  */
  root  TEXT UNIQUE NOT NULL,

  /* the UUID of the repository */
  uuid  TEXT NOT NULL
  );

/* Note: a repository (identified by its UUID) may appear at multiple URLs.
   For example, http://example.com/repos/ and https://example.com/repos/.  */
CREATE INDEX I_UUID ON REPOSITORY (uuid);
CREATE INDEX I_ROOT ON REPOSITORY (root);


/* ------------------------------------------------------------------------- */

CREATE TABLE WCROOT (
  id  INTEGER PRIMARY KEY AUTOINCREMENT,

  /* absolute path in the local filesystem.  NULL if storing metadata in
     the wcroot itself. */
  local_abspath  TEXT UNIQUE
  );

CREATE UNIQUE INDEX I_LOCAL_ABSPATH ON WCROOT (local_abspath);


/* ------------------------------------------------------------------------- */

/* The PRISTINE table keeps track of pristine texts.  Each row describes a
   single pristine text.  The text itself is stored in a file whose name is
   derived from the 'checksum' column.  Each pristine text is referenced by
   any number of rows in the NODES and ACTUAL_NODE tables.

   In future, the pristine text file may be compressed.
 */
CREATE TABLE PRISTINE (
  /* The SHA-1 checksum of the pristine text. This is a unique key. The
     SHA-1 checksum of a pristine text is assumed to be unique among all
     pristine texts referenced from this database. */
  checksum  TEXT NOT NULL PRIMARY KEY,

  /* Enumerated values specifying type of compression. The only value
     supported so far is NULL, meaning that no compression has been applied
     and the pristine text is stored verbatim in the file. */
  compression  INTEGER,

  /* The size in bytes of the file in which the pristine text is stored.
     Used to verify the pristine file is "proper". */
  size  INTEGER NOT NULL,

  /* The number of rows in the NODES table that have a 'checksum' column
     value that refers to this row.  (References in other places, such as
     in the ACTUAL_NODE table, are not counted.) */
  refcount  INTEGER NOT NULL,

  /* Alternative MD5 checksum used for communicating with older
     repositories. Not strictly guaranteed to be unique among table rows. */
  md5_checksum  TEXT NOT NULL
  );

CREATE INDEX I_PRISTINE_MD5 ON PRISTINE (md5_checksum);

/* ------------------------------------------------------------------------- */

/* The ACTUAL_NODE table describes text changes and property changes
   on each node in the WC, relative to the NODES table row for the
   same path. (A NODES row must exist if this node exists, but an
   ACTUAL_NODE row can exist on its own if it is just recording info
   on a non-present node - a tree conflict or a changelist, for
   example.)

   The ACTUAL_NODE table row for a given path exists if the node at that
   path is known to have text or property changes relative to its
   NODES row. ("Is known" because a text change on disk may not yet
   have been discovered and recorded here.)

   The ACTUAL_NODE table row for a given path may also exist in other cases,
   including if the "changelist" or any of the conflict columns have a
   non-null value.
 */
CREATE TABLE ACTUAL_NODE (
  /* specifies the location of this node in the local filesystem */
  wc_id  INTEGER NOT NULL REFERENCES WCROOT (id),
  local_relpath  TEXT NOT NULL,

  /* parent's local_relpath for aggregating children of a given parent.
     this will be "" if the parent is the wcroot. NULL if this is the
     wcroot node. */
  parent_relpath  TEXT,

  /* serialized skel of this node's properties. NULL implies no change to
     the properties, relative to WORKING/BASE as appropriate. */
  properties  BLOB,

  /* relpaths of the conflict files. */
  /* ### These columns will eventually be merged into conflict_data below. */
  conflict_old  TEXT,
  conflict_new  TEXT,
  conflict_working  TEXT,
  prop_reject  TEXT,

  /* if not NULL, this node is part of a changelist. */
  changelist  TEXT,

  /* ### need to determine values. "unknown" (no info), "admin" (they
     ### used something like 'svn edit'), "noticed" (saw a mod while
     ### scanning the filesystem). */
  text_mod  TEXT,

  /* if a directory, serialized data for all of tree conflicts therein.
     ### This column will eventually be merged into the conflict_data column,
     ### but within the ACTUAL node of the tree conflict victim itself, rather
     ### than the node of the tree conflict victim's parent directory. */
  tree_conflict_data  TEXT,

  /* A skel containing the conflict details.  */
  conflict_data  BLOB,

  /* Three columns containing the checksums of older, left and right conflict
     texts.  Stored in a column to allow storing them in the pristine store  */
  /* stsp: This is meant for text conflicts, right? What about property
           conflicts? Why do we need these in a column to refer to the
           pristine store? Can't we just parse the checksums from
           conflict_data as well?
     rhuijben: Because that won't allow triggers to handle refcounts.
               We would have to scan all conflict skels before cleaning up the
               a single file from the pristine stor */
  older_checksum  TEXT REFERENCES PRISTINE (checksum),
  left_checksum  TEXT REFERENCES PRISTINE (checksum),
  right_checksum  TEXT REFERENCES PRISTINE (checksum),

  PRIMARY KEY (wc_id, local_relpath)
  );

CREATE UNIQUE INDEX I_ACTUAL_PARENT ON ACTUAL_NODE (wc_id, parent_relpath,
                                                    local_relpath);


/* ------------------------------------------------------------------------- */

/* This table is a cache of information about repository locks. */
CREATE TABLE LOCK (
  /* what repository location is locked */
  repos_id  INTEGER NOT NULL REFERENCES REPOSITORY (id),
  repos_relpath  TEXT NOT NULL,

  /* Information about the lock. Note: these values are just caches from
     the server, and are not authoritative. */
  lock_token  TEXT NOT NULL,
  /* ### make the following fields NOT NULL ? */
  lock_owner  TEXT,
  lock_comment  TEXT,
  lock_date  INTEGER,   /* an APR date/time (usec since 1970) */

  PRIMARY KEY (repos_id, repos_relpath)
  );


/* ------------------------------------------------------------------------- */

CREATE TABLE WORK_QUEUE (
  /* Work items are identified by this value.  */
  id  INTEGER PRIMARY KEY AUTOINCREMENT,

  /* A serialized skel specifying the work item.  */
  work  BLOB NOT NULL
  );


/* ------------------------------------------------------------------------- */

CREATE TABLE WC_LOCK (
  /* specifies the location of this node in the local filesystem */
  wc_id  INTEGER NOT NULL  REFERENCES WCROOT (id),
  local_dir_relpath  TEXT NOT NULL,

  locked_levels  INTEGER NOT NULL DEFAULT -1,

  PRIMARY KEY (wc_id, local_dir_relpath)
 );


/* ------------------------------------------------------------------------- */

/* The NODES table describes the way WORKING nodes are layered on top of
   BASE nodes and on top of other WORKING nodes, due to nested tree structure
   changes. The layers are modelled using the "op_depth" column.

   An 'operation depth' refers to the number of directory levels down from
   the WC root at which a tree-change operation (delete, add?, copy, move)
   was performed.  A row's 'op_depth' does NOT refer to the depth of its own
   'local_relpath', but rather to the depth of the nearest tree change that
   affects that node.

   The row with op_depth=0 for any given local relpath represents the "base"
   node that is created and updated by checkout, update, switch and commit
   post-processing.  The row with the highest op_depth for a particular
   local_relpath represents the working version.  Any rows with intermediate
   op_depth values are not normally visible to the user but may become
   visible after reverting local changes.

   This table contains full node descriptions for nodes in either the BASE
   or WORKING trees as described in notes/wc-ng/design. Fields relate
   both to BASE and WORKING trees, unless documented otherwise.

   For illustration, with a scenario like this:

     # (0)
     svn rm foo
     svn cp ^/moo foo   # (1)
     svn rm foo/bar
     touch foo/bar
     svn add foo/bar    # (2)

   , these are the NODES table rows for the path foo/bar:

   (0)  "BASE" --->  NODES (op_depth == 0)
   (1)               NODES (op_depth == 1)
   (2)               NODES (op_depth == 2)

   0 is the original data for foo/bar before 'svn rm foo' (if it existed).
   1 is the data for foo/bar copied in from ^/moo/bar.
   2 is the to-be-committed data for foo/bar, created by 'svn add foo/bar'.

   An 'svn revert foo/bar' would remove the NODES of (2).

 */
CREATE TABLE NODES (
  /* Working copy location related fields */

  wc_id  INTEGER NOT NULL REFERENCES WCROOT (id),
  local_relpath  TEXT NOT NULL,

  /* Contains the depth (= number of path segments) of the operation
     modifying the working copy tree structure. All nodes below the root
     of the operation (aka operation root, aka oproot) affected by the
     operation will be assigned the same op_depth.

     op_depth == 0 designates the initial checkout; the BASE tree.

   */
  op_depth INTEGER NOT NULL,

  /* parent's local_relpath for aggregating children of a given parent.
     this will be "" if the parent is the wcroot.  Since a wcroot will
     never have a WORKING node the parent_relpath will never be null,
     except when op_depth == 0 and the node is a wcroot. */
  parent_relpath  TEXT,


  /* Repository location fields */

  /* When op_depth == 0, these fields refer to the repository location of the
     BASE node, the location of the initial checkout.

     When op_depth != 0, they indicate where this node was copied/moved from.
     In this case, the fields are set for the root of the operation and for all
     children. */
  repos_id  INTEGER REFERENCES REPOSITORY (id),
  repos_path  TEXT,
  revision  INTEGER,


  /* WC state fields */

  /* The tree state of the node.

     In case 'op_depth' is equal to 0, this node is part of the 'BASE'
     tree.  The 'BASE' represents pristine nodes that are in the
     repository; it is obtained and modified by commands such as
     checkout/update/switch.

     In case 'op_depth' is greater than 0, this node is part of a
     layer of working nodes.  The 'WORKING' tree is obtained and
     modified by commands like delete/copy/revert.

     The 'BASE' and 'WORKING' trees use the same literal values for
     the 'presence' but the meaning of each value can vary depending
     on the tree.

     normal: in the 'BASE' tree this is an ordinary node for which we
       have full information.  In the 'WORKING' tree it's an added or
       copied node for which we have full information.

     not-present: in the 'BASE' tree this is a node that is implied to
       exist by the parent node, but is not present in the working
       copy.  Typically obtained by delete/commit, or by update to
       revision in which the node does not exist.  In the 'WORKING'
       tree this is a copy of a 'not-present' node from the 'BASE'
       tree, and it will be deleted on commit.  Such a node cannot be
       copied directly, but can be copied as a descendant.

     incomplete: in the 'BASE' tree this is an ordinary node for which
       we do not have full information.  Only the name is guaranteed;
       we may not have all its children, we may not have its checksum,
       etc.  In the 'WORKING' tree this is a copied node for which we
       do not have the full information.  This state is generally
       obtained when an operation was interrupted.

     base-deleted: not valid in 'BASE' tree.  In the 'WORKING' tree
       this represents a node that is deleted from the tree below the
       current 'op_depth'.  This state is badly named, it should be
       something like 'deleted'.

     server-excluded: in the 'BASE' tree this is a node that is excluded by
       authz.  The name of the node is known from the parent, but no
       other information is available.  Not valid in the 'WORKING'
       tree as there is no way to commit such a node.

     excluded: in the 'BASE' tree this node is administratively
       excluded by the user (sparse WC).  In the 'WORKING' tree this
       is a copy of an excluded node from the 'BASE' tree.  Such a
       node cannot be copied directly but can be copied as a
       descendant. */

  presence  TEXT NOT NULL,

  /* ### JF: For an old-style move, "copyfrom" info stores its source, but a
     new WC-NG "move" is intended to be a "true rename" so its copyfrom
     revision is implicit, being in effect (new head - 1) at commit time.
     For a (new) move, we need to store or deduce the copyfrom local-relpath;
     perhaps add a column called "moved_from". */

  /* Boolean value, specifying if this node was moved here (rather than just
     copied). This is set on all the nodes in the moved tree.  The source of
     the move is implied by a different node with a moved_to column pointing
     at the root node of the moved tree. */
  moved_here  INTEGER,

  /* If the underlying node was moved away (rather than just deleted), this
     specifies the local_relpath of where the node was moved to.
     This is set only on the root of a move, and is NULL for all children.

     The op-depth of the moved-to node is not recorded. A moved_to path
     always points at a node within the highest op-depth layer at the
     destination. This invariant must be maintained by operations which
     change existing move information. */
  moved_to  TEXT,


  /* Content fields */

  /* the kind of the new node. may be "unknown" if the node is not present. */
  kind  TEXT NOT NULL,

  /* serialized skel of this node's properties (when presence is 'normal' or
     'incomplete'); an empty skel or NULL indicates no properties.  NULL if
     we have no information about the properties (any other presence).
     TODO: Choose & require a single representation for 'no properties'.
  */
  properties  BLOB,

  /* NULL depth means "default" (typically svn_depth_infinity) */
  /* ### depth on WORKING? seems this is a BASE-only concept. how do
     ### you do "files" on an added-directory? can't really ignore
     ### the subdirs! */
  /* ### maybe a WC-to-WC copy can retain a depth?  */
  depth  TEXT,

  /* The SHA-1 checksum of the pristine text, if this node is a file and was
     moved here or copied here, else NULL. */
  checksum  TEXT REFERENCES PRISTINE (checksum),

  /* for kind==symlink, this specifies the target. */
  symlink_target  TEXT,


  /* Last-Change fields */

  /* If this node was moved here or copied here, then the following fields may
     have information about their source node.  changed_rev must be not-null
     if this node has presence=="normal". changed_date and changed_author may
     be null if the corresponding revprops are missing.

     For an added or not-present node, these are null.  */
  changed_revision  INTEGER,
  changed_date      INTEGER,  /* an APR date/time (usec since 1970) */
  changed_author    TEXT,


  /* Various cache fields */

  /* The size in bytes of the working file when it had no local text
     modifications. This means the size of the text when translated from
     repository-normal format to working copy format with EOL style
     translated and keywords expanded according to the properties in the
     "properties" column of this row.

     NULL if this node is not a file or if the size has not (yet) been
     computed. */
  translated_size  INTEGER,

  /* The mod-time of the working file when it was last determined to be
     logically unmodified relative to its base, taking account of keywords
     and EOL style. This value is used in the change detection heuristic
     used by the status command.

     NULL if this node is not a file or if this info has not yet been
     determined.
   */
  last_mod_time  INTEGER,  /* an APR date/time (usec since 1970) */

  /* serialized skel of this node's dav-cache.  could be NULL if the
     node does not have any dav-cache. */
  dav_cache  BLOB,

  /* Is there a file external in this location. NULL if there
     is no file external, otherwise '1'  */
  /* ### Originally we had a wc-1.0 like skel in this place, so we
     ### check for NULL.
     ### In Subversion 1.7 we defined this column as TEXT, but Sqlite
     ### only uses this information for deciding how to optimize
     ### anyway. */
  file_external  INTEGER,

  /* serialized skel of this node's inherited properties. NULL if this
     is not the BASE of a WC root node. */
  inherited_props  BLOB,

  PRIMARY KEY (wc_id, local_relpath, op_depth)

  );

CREATE UNIQUE INDEX I_NODES_PARENT ON NODES (wc_id, parent_relpath,
                                             local_relpath, op_depth);
/* I_NODES_MOVED is introduced in format 30 */
CREATE UNIQUE INDEX I_NODES_MOVED ON NODES (wc_id, moved_to, op_depth);

/* Many queries have to filter the nodes table to pick only that version
   of each node with the highest (most "current") op_depth.  This view
   does the heavy lifting for such queries.

   Note that this view includes a row for each and every path that is known
   in the WC, including, for example, paths that were children of a base- or
   lower-op-depth directory that has been replaced by something else in the
   current view.
 */
CREATE VIEW NODES_CURRENT AS
  SELECT * FROM nodes AS n
    WHERE op_depth = (SELECT MAX(op_depth) FROM nodes AS n2
                      WHERE n2.wc_id = n.wc_id
                        AND n2.local_relpath = n.local_relpath);

/* Many queries have to filter the nodes table to pick only that version
   of each node with the BASE ("as checked out") op_depth.  This view
   does the heavy lifting for such queries. */
CREATE VIEW NODES_BASE AS
  SELECT * FROM nodes
  WHERE op_depth = 0;

CREATE TRIGGER nodes_insert_trigger
AFTER INSERT ON nodes
WHEN NEW.checksum IS NOT NULL
BEGIN
  UPDATE pristine SET refcount = refcount + 1
  WHERE checksum = NEW.checksum;
END;

CREATE TRIGGER nodes_delete_trigger
AFTER DELETE ON nodes
WHEN OLD.checksum IS NOT NULL
BEGIN
  UPDATE pristine SET refcount = refcount - 1
  WHERE checksum = OLD.checksum;
END;

CREATE TRIGGER nodes_update_checksum_trigger
AFTER UPDATE OF checksum ON nodes
WHEN NEW.checksum IS NOT OLD.checksum
  /* AND (NEW.checksum IS NOT NULL OR OLD.checksum IS NOT NULL) */
BEGIN
  UPDATE pristine SET refcount = refcount + 1
  WHERE checksum = NEW.checksum;
  UPDATE pristine SET refcount = refcount - 1
  WHERE checksum = OLD.checksum;
END;

CREATE TABLE EXTERNALS (
  /* Working copy location related fields (like NODES)*/

  wc_id  INTEGER NOT NULL REFERENCES WCROOT (id),
  local_relpath  TEXT NOT NULL,

  /* The working copy root can't be recorded as an external in itself
     so this will never be NULL. ### ATM only inserted, never queried */
  parent_relpath  TEXT NOT NULL,

  /* Repository location fields */
  repos_id  INTEGER NOT NULL REFERENCES REPOSITORY (id),

  /* Either MAP_NORMAL or MAP_EXCLUDED */
  presence  TEXT NOT NULL,

  /* the kind of the external. */
  kind  TEXT NOT NULL,

  /* The local relpath of the directory NODE defining this external
     (Defaults to the parent directory of the file external after upgrade) */
  def_local_relpath         TEXT NOT NULL,

  /* The url of the external as used in the definition */
  def_repos_relpath         TEXT NOT NULL,

  /* The operational (peg) and node revision if this is a revision fixed
     external; otherwise NULL. (Usually these will both have the same value) */
  def_operational_revision  TEXT,
  def_revision              TEXT,

  PRIMARY KEY (wc_id, local_relpath)
);

CREATE UNIQUE INDEX I_EXTERNALS_DEFINED ON EXTERNALS (wc_id,
                                                      def_local_relpath,
                                                      local_relpath);


PRAGMA user_version =
-- define: SVN_WC__VERSION
;


/* ------------------------------------------------------------------------- */
/* This statement provides SQLite with the necessary information about our
   indexes to make better decisions in the query planner.

   For every interesting index this contains a number of rows where the
   statistics are calculated for and then for every column in the index the
   average number of rows with the same value in all columns left of this
   column including the column itself.

   See http://www.sqlite.org/fileformat2.html#stat1tab for more details.

   The important thing here is that this tells Sqlite that the wc_id column
   of the NODES and ACTUAL_NODE table is usually a single value, so queries
   should use more than one column for index usage.

   The current hints describe NODES+ACTUAL_NODE as a working copy with
   8000 nodes in 1 a single working copy(=wc_id), 10 nodes per directory
   and an average of 2 op-depth layers per node.

   The number of integers must be number of index columns + 1, which is
   verified via the test_schema_statistics() test.
 */
-- STMT_INSTALL_SCHEMA_STATISTICS
ANALYZE sqlite_master; /* Creates empty sqlite_stat1 if necessary */

DELETE FROM sqlite_stat1
WHERE tbl in ('NODES', 'ACTUAL_NODE', 'LOCK', 'WC_LOCK', 'EXTERNALS');

INSERT INTO sqlite_stat1(tbl, idx, stat) VALUES
    ('NODES', 'sqlite_autoindex_NODES_1',               '8000 8000 2 1');
INSERT INTO sqlite_stat1(tbl, idx, stat) VALUES
    ('NODES', 'I_NODES_PARENT',                         '8000 8000 10 2 1');
/* Tell a lie: We ignore that 99.9% of all moved_to values are NULL */
INSERT INTO sqlite_stat1(tbl, idx, stat) VALUES
    ('NODES', 'I_NODES_MOVED',                          '8000 8000 1 1');

INSERT INTO sqlite_stat1(tbl, idx, stat) VALUES
    ('ACTUAL_NODE', 'sqlite_autoindex_ACTUAL_NODE_1',   '8000 8000 1');
INSERT INTO sqlite_stat1(tbl, idx, stat) VALUES
    ('ACTUAL_NODE', 'I_ACTUAL_PARENT',                  '8000 8000 10 1');

INSERT INTO sqlite_stat1(tbl, idx, stat) VALUES
    ('LOCK', 'sqlite_autoindex_LOCK_1',                 '100 100 1');

INSERT INTO sqlite_stat1(tbl, idx, stat) VALUES
    ('WC_LOCK', 'sqlite_autoindex_WC_LOCK_1',           '100 100 1');

INSERT INTO sqlite_stat1(tbl, idx, stat) VALUES
    ('EXTERNALS','sqlite_autoindex_EXTERNALS_1',        '100 100 1');
INSERT INTO sqlite_stat1(tbl, idx, stat) VALUES
    ('EXTERNALS','I_EXTERNALS_DEFINED',                 '100 100 3 1');

/* sqlite_autoindex_WORK_QUEUE_1 doesn't exist because WORK_QUEUE is
   a INTEGER PRIMARY KEY AUTOINCREMENT table */

ANALYZE sqlite_master; /* Loads sqlite_stat1 data for query optimizer */
/* ------------------------------------------------------------------------- */

/* Format 30 creates a new NODES index for move information, and a new
   PRISTINE index for the md5_checksum column. It also activates use of
   skel-based conflict storage -- see notes/wc-ng/conflict-storage-2.0.
   It also renames the "absent" presence to "server-excluded". */
-- STMT_UPGRADE_TO_30
CREATE UNIQUE INDEX IF NOT EXISTS I_NODES_MOVED
ON NODES (wc_id, moved_to, op_depth);

CREATE INDEX IF NOT EXISTS I_PRISTINE_MD5 ON PRISTINE (md5_checksum);

UPDATE nodes SET presence = "server-excluded" WHERE presence = "absent";

/* Just to be sure clear out file external skels from pre 1.7.0 development
   working copies that were never updated by 1.7.0+ style clients */
UPDATE nodes SET file_external=1 WHERE file_external IS NOT NULL;

-- STMT_UPGRADE_30_SELECT_CONFLICT_SEPARATE
SELECT wc_id, local_relpath,
  conflict_old, conflict_working, conflict_new, prop_reject, tree_conflict_data
FROM actual_node
WHERE conflict_old IS NOT NULL
   OR conflict_working IS NOT NULL
   OR conflict_new IS NOT NULL
   OR prop_reject IS NOT NULL
   OR tree_conflict_data IS NOT NULL
ORDER by wc_id, local_relpath

-- STMT_UPGRADE_30_SET_CONFLICT
UPDATE actual_node SET conflict_data = ?3, conflict_old = NULL,
  conflict_working = NULL, conflict_new = NULL, prop_reject = NULL,
  tree_conflict_data = NULL
WHERE wc_id = ?1 and local_relpath = ?2

/* ------------------------------------------------------------------------- */

/* Format 31 adds the inherited_props column to the NODES table. C code then
   initializes the update/switch roots to make sure future updates fetch the
   inherited properties */
-- STMT_UPGRADE_TO_31
ALTER TABLE NODES ADD COLUMN inherited_props BLOB;

DROP INDEX IF EXISTS I_ACTUAL_CHANGELIST;
DROP INDEX IF EXISTS I_EXTERNALS_PARENT;

DROP INDEX I_NODES_PARENT;
CREATE UNIQUE INDEX I_NODES_PARENT ON NODES (wc_id, parent_relpath,
                                             local_relpath, op_depth);

DROP INDEX I_ACTUAL_PARENT;
CREATE UNIQUE INDEX I_ACTUAL_PARENT ON ACTUAL_NODE (wc_id, parent_relpath,
                                                    local_relpath);

PRAGMA user_version = 31;

-- STMT_UPGRADE_31_SELECT_WCROOT_NODES
/* Select all base nodes which are the root of a WC, including
   switched subtrees, but excluding those which map to the root
   of the repos.

   ### IPROPS: Is this query horribly inefficient?  Quite likely,
   ### but it only runs during an upgrade, so do we care? */
SELECT l.wc_id, l.local_relpath FROM nodes as l
LEFT OUTER JOIN nodes as r
ON l.wc_id = r.wc_id
   AND r.local_relpath = l.parent_relpath
   AND r.op_depth = 0
WHERE l.op_depth = 0
  AND l.repos_path != ''
  AND ((l.repos_id IS NOT r.repos_id)
       OR (l.repos_path IS NOT RELPATH_SKIP_JOIN(r.local_relpath, r.repos_path, l.local_relpath)))


/* ------------------------------------------------------------------------- */
/* Format 32 ....  */
/* -- STMT_UPGRADE_TO_32
PRAGMA user_version = 32; */


/* ------------------------------------------------------------------------- */

/* Format 99 drops all columns not needed due to previous format upgrades.
   Before we release 1.7, these statements will be pulled into a format bump
   and all the tables will be cleaned up. We don't know what that format
   number will be, however, so we're just marking it as 99 for now.  */
-- format: 99

/* TODO: Un-confuse *_revision column names in the EXTERNALS table to
   "-r<operative> foo@<peg>", as suggested by the patch attached to
   http://svn.haxx.se/dev/archive-2011-09/0478.shtml */
/* TODO: Remove column parent_relpath from EXTERNALS. We're not using it and
   never will. It's not interesting like in the NODES table: the external's
   parent path may be *anything*: unversioned, "behind" a another WC... */

/* Now "drop" the tree_conflict_data column from actual_node. */
CREATE TABLE ACTUAL_NODE_BACKUP (
  wc_id  INTEGER NOT NULL,
  local_relpath  TEXT NOT NULL,
  parent_relpath  TEXT,
  properties  BLOB,
  conflict_old  TEXT,
  conflict_new  TEXT,
  conflict_working  TEXT,
  prop_reject  TEXT,
  changelist  TEXT,
  text_mod  TEXT
  );

INSERT INTO ACTUAL_NODE_BACKUP SELECT
  wc_id, local_relpath, parent_relpath, properties, conflict_old,
  conflict_new, conflict_working, prop_reject, changelist, text_mod
FROM ACTUAL_NODE;

DROP TABLE ACTUAL_NODE;

CREATE TABLE ACTUAL_NODE (
  wc_id  INTEGER NOT NULL REFERENCES WCROOT (id),
  local_relpath  TEXT NOT NULL,
  parent_relpath  TEXT,
  properties  BLOB,
  conflict_old  TEXT,
  conflict_new  TEXT,
  conflict_working  TEXT,
  prop_reject  TEXT,
  changelist  TEXT,
  text_mod  TEXT,

  PRIMARY KEY (wc_id, local_relpath)
  );

CREATE UNIQUE INDEX I_ACTUAL_PARENT ON ACTUAL_NODE (wc_id, parent_relpath,
                                                    local_relpath);

INSERT INTO ACTUAL_NODE SELECT
  wc_id, local_relpath, parent_relpath, properties, conflict_old,
  conflict_new, conflict_working, prop_reject, changelist, text_mod
FROM ACTUAL_NODE_BACKUP;

DROP TABLE ACTUAL_NODE_BACKUP;

/* Note: Other differences between the schemas of an upgraded and a
 * fresh WC.
 *
 * While format 22 was current, "NOT NULL" was added to the
 * columns PRISTINE.size and PRISTINE.md5_checksum.  The format was not
 * bumped because it is a forward- and backward-compatible change.
 *
 * While format 23 was current, "REFERENCES PRISTINE" was added to the
 * columns ACTUAL_NODE.older_checksum, ACTUAL_NODE.left_checksum,
 * ACTUAL_NODE.right_checksum, NODES.checksum.
 *
 * The "NODES_BASE" view was originally implemented with a more complex (but
 * functionally equivalent) statement using a 'JOIN'.  WCs that were created
 * at or upgraded to format 26 before it was changed will still have the old
 * version.
 */

