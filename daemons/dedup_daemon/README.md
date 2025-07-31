# HER OS Deduplication Daemon

## Overview
This daemon provides asynchronous, policy-driven, block-level deduplication for HER OS, leveraging BTRFS reflink and advanced metadata integration.

## Key Features
- **Async, Idle-Friendly:** Runs as a low-priority, async background service.
- **Policy-Driven:** Queries the Metadata Daemon for deduplication candidates (e.g., large, static, or user-tagged files).
- **Block-Level Deduplication:** Uses BTRFS reflink/ioctl to deduplicate matching blocks between files, saving space with no user impact.
- **No Real-Time Overhead:** Deduplication is never performed inline with user writes.
- **Logging:** All dedup actions and stats are logged for audit and monitoring.
- **Integration:** Designed to work with HER OS Metadata Daemon, BTRFS, and systemd.

## Operation
1. **Candidate Selection:**
   - Periodically queries the Metadata Daemon for files matching deduplication policies.
2. **Block Hashing:**
   - Computes hashes for candidate files to identify duplicates.
3. **Deduplication:**
   - Uses BTRFS reflink/ioctl to merge duplicate blocks/files.
4. **Idle Scheduling:**
   - Runs only when system is idle or under low load.
5. **Reporting:**
   - Logs dedup actions and space savings.

## Configuration
- Policy rules (file types, tags, size thresholds)
- Scheduling (idle time, frequency)
- Logging options

## Usage
- `cargo run --release` or install as a systemd service
- Logs to `/var/log/heros_dedup.log`

## Extension Points
- Add new dedup policies or candidate selection strategies
- Integrate with system resource monitors for smarter scheduling
- Extend logging/metrics for observability
- Add support for other filesystems with reflink support 