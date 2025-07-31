# Data Tiering Daemon

Manages hot/cold data migration using BTRFS send/receive and snapshotting.

## Implementation (C)
- `tiering_daemon.c`: Main daemon, written in C for performance and Linux compatibility.
- Listens on a Unix domain socket (`/tmp/heros_tiering_daemon.sock`).
- Handles migration requests with a simple protocol.
- Stubs for future migration logic (BTRFS commands, metadata updates).

## Protocol
Send a single line (UTF-8) per request. Supported commands:
- `MIGRATE <subvol_path> <target_tier> [SNAPSHOT_NAME]` (target_tier: HOT | COLD)
- `HELP`

Responses:
- `OK: Migration started`
- `ERR: <reason>`

## Usage
Compile:
```sh
gcc -o tiering_daemon tiering_daemon.c
```
Run:
```sh
./tiering_daemon
``` 