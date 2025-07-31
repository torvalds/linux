# Policy Decision Point (PDP)

Handles access control decisions for the HER OS custom LSM.

## Implementation (C)
- `pdp_daemon.c`: Main daemon, written in C for performance and Linux compatibility.
- Communicates with the kernel via Netlink (NETLINK_GENERIC, protocol 31).
- Listens for access control requests and replies with allow/deny verdicts.
- Supports advanced policy evaluation (path, subvol_id, snapshot, subvolname, user, hours).

## Protocol
- Kernel sends access request (process info, file inode, requested permission, subvol_id, snapshot, subvolname, uid) as a Netlink message.
- PDP evaluates policy (see below).
- PDP replies with `ALLOW` or `DENY` as a Netlink message.

## Policy File Format (`pdp_policy.txt`)
Each line is a rule. First matching rule wins.

Supported rule types:
- `allow|deny <path>`
- `allow|deny subvol:<id>`
- `allow|deny snapshot:<n>`
- `allow|deny subvolname:<name>`
- `allow|deny user:<username>`
- `allow|deny hours:<start>-<end>` (hours in 24h format, e.g., 8-18)

### Examples
```
deny /forbidden.txt
allow user:alice
allow hours:8-18
allow subvol:12345
deny snapshot:1
allow *
```
- The above denies access to `/forbidden.txt` for everyone.
- Allows access for user `alice`.
- Allows access during 8:00-18:00 for all users.
- Allows access to subvolume 12345.
- Denies access to snapshot 1.
- Allows everything else by default.

## Advanced Policy Rule Types

### Group-Based Rules
- Syntax: `allow group:admin`
- Example: `deny group:guests`
- Matches if the requesting user is a member of the specified group.

### Expression-Based Rules
- Syntax: `allow expr:uid==1000&&hour>=8&&hour<=18`
- Example: `deny expr:uid==1001&&hour<8`
- Supports simple expressions: `uid==X`, `hour>=Y`, `hour<=Z` (can be combined with &&).
- Matches if all expressions are true for the request.

### Evaluation Order
- First matching rule wins (as before).
- Rules are checked in order: path, subvol_id, snapshot, subvolname, user, group, hours, expr.

### Usage Notes
- Group membership is checked using system groups.
- Expression evaluation is currently limited to `uid` and `hour` (extension point for more complex logic).
- See `pdp_policy.txt` for examples.

## Usage
Compile:
```sh
gcc -o pdp_daemon pdp_daemon.c
```
Run:
```sh
./pdp_daemon
```

## Extension Points
- Future: group-based, expression-based, or context-aware rules.
- See code comments for further extension and maintenance guidelines. 