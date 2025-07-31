# HER OS WAL/2PC Daemon

## Overview
This daemon provides a system-wide Write-Ahead Log (WAL) and Two-Phase Commit (2PC) protocol for HER OS, ensuring transactional consistency between the physical filesystem and the Metadata Daemon.

## Key Features
- **Event Monitoring:** Uses eBPF, fanotify, or inotify to monitor all file operations (create, write, rename, delete).
- **Write-Ahead Log:** Appends all file operation intents to a high-speed WAL file for durability.
- **WAL Rotation/Archival:** WAL file is rotated when it exceeds 100MB; old WALs are archived to `/var/log/heros_wal_archive/` with a timestamped name.
- **Two-Phase Commit:** Coordinates with the Metadata Daemon to ensure operations are only marked as processed after successful DB commit.
- **Crash Recovery:** On startup, replays any unprocessed WAL entries to bring the semantic DB in sync with the physical filesystem.
- **Distributed/Clustered Ready:** Extension points for 2PC across multiple nodes/services.
- **Metrics and Logging:** Logs all operations, errors, and recovery actions.

## WAL Rotation/Archival
- **Policy:** WAL file is rotated when it exceeds 100MB in size.
- **Archive Directory:** Rotated WAL files are moved to `/var/log/heros_wal_archive/`.
- **Naming Scheme:** Archived files are named `heros_wal_<YYYYMMDD_HHMMSS>.log`.
- **Extension Points:** Future support for max age, retention policy, or compression of archived WALs.

## Operation
1. **Event Capture:**
   - Monitors file operations using eBPF/fanotify/inotify.
2. **WAL Append:**
   - Appends each operation as an intent record to the WAL file.
   - Rotates WAL file and archives old logs as needed.
3. **2PC Coordination:**
   - Metadata Daemon reads WAL, processes, and marks entries as committed.
4. **Crash Recovery:**
   - On restart, WAL is replayed to ensure no lost or orphaned files.

## Configuration
- WAL file path and rotation policy (see constants in code)
- Archive directory for rotated WALs
- Event source (eBPF, fanotify, inotify)
- 2PC/commit protocol settings
- Logging and metrics options

## Usage
- `cargo run --release` or install as a systemd service
- Logs to `/var/log/heros_wal.log` and archives to `/var/log/heros_wal_archive/`

## Extension Points
- Add distributed/clustered 2PC for multi-node deployments
- Integrate with HER OS monitoring and orchestration
- Add richer event filtering or policy modules
- Add max age, retention, or compression for archived WALs 