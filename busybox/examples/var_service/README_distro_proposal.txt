	A distro which already uses runit

I installed Void Linux, in order to see what do they have.
Xfce desktop looks fairly okay, network is up.
ps tells me they did put X, dbus, NM and udev into runsvdir-supervised tree:

    1 ?        00:00:01 runit
  623 ?        00:00:00   runsvdir
  629 ?        00:00:00     runsv
  650 tty1     00:00:00       agetty
  630 ?        00:00:00     runsv
  644 ?        00:00:09       NetworkManager
 1737 ?        00:00:00         dhclient
  631 ?        00:00:00     runsv
  639 tty4     00:00:00       agetty
  632 ?        00:00:00     runsv
  640 ?        00:00:00       sshd
 1804 ?        00:00:00         sshd
 1809 pts/3    00:00:00           sh
 1818 pts/3    00:00:00             ps
  633 ?        00:00:00     runsv
  637 tty5     00:00:00       agetty
  634 ?        00:00:00     runsv
  796 ?        00:00:00       dhclient
  635 ?        00:00:00     runsv
  649 ?        00:00:00       uuidd
  636 ?        00:00:00     runsv
  647 ?        00:00:00       acpid
  638 ?        00:00:00     runsv
  652 ?        00:00:00       console-kit-dae
  641 ?        00:00:00     runsv
  651 tty6     00:00:00       agetty
  642 ?        00:00:00     runsv
  660 tty2     00:00:00       agetty
  643 ?        00:00:00     runsv
  657 ?        00:00:02       dbus-daemon
  645 ?        00:00:00     runsv
  658 ?        00:00:00       cgmanager
  648 ?        00:00:00     runsv
  656 tty3     00:00:00       agetty
  653 ?        00:00:00     runsv
  655 ?        00:00:00       lxdm-binary
  698 tty7     00:00:14         Xorg
  729 ?        00:00:00         lxdm-session
  956 ?        00:00:00           sh
  982 ?        00:00:00             xfce4-session
 1006 ?        00:00:04               nm-applet
  654 ?        00:00:00     runsv
  659 ?        00:00:00       udevd

Here is a link to Void Linux's wiki:

    https://wiki.voidlinux.eu/Runit

Void Linux packages install their services as subdirectories of /etc/rc,
such as /etc/sv/sshd, with a script file, "run", and a link
"supervise" -> /run/runit/supervise.sshd

For sshd, "run" contains:

    #!/bin/sh
    ssh-keygen -A >/dev/null 2>&1 # generate host keys if they don't exist
    [ -r conf ] && . ./conf
    exec /usr/bin/sshd -D $OPTS

That's it from the POV of the packager.

This is pretty minimalistic, and yet, it is already distro-specific:
the link to /run/runit/* is conceptually wrong, it requires packagers
to know that /etc/rc should not be mutable and thus they need to use
a different location in filesystem for supervise/ directory.

I think a good thing would be to require just one file: the "run" script.
The rest should be handled by distro tooling, not by packager.

A similar issue is arising with logging. It would be ideal if packagers
would not need to know how a particular distro manages logs.
Whatever their daemons print to stdout/stderr, should be automagically logged
in a way distro prefers.

* * * * * * * *

	Proposed "standard" on how distros should use runit

The original idea of services-as-directories belongs to D.J.Bernstein (djb),
and his project to implement it is daemontools: https://cr.yp.to/daemontools.html

There are several reimplementations of daemontools:
- runit: by Gerrit Pape, http://smarden.org/runit/
  (busybox has it included)
- s6: by Laurent Bercot, http://skarnet.org/software/s6/

It is not required that a specific clone should be used. Let evolution work.


	Terminology

daemon: any long running background program. Common examples are sshd, getty,
ntpd, dhcp client...

service: daemon controlled by a service monitor.

service directory: a directory with an executable file (script) named "run"
which (usually) execs some daemon, possibly after some preparatory steps.
It should start it not as a child or daemonized process, but by exec'ing it
(inheriting the same PID and the place in the process tree).

service monitor: a tool which watches a set of service directories.
In daemontools package, it is called "svscan". In runit, it is called
"runsvdir". In s6, it is called "s6-svscan".
Service monitor starts a supervisor for each service directory.
If it dies, it restarts it. If service directory disappears,
service monitor will not be restarted if it dies.
runit's service monitor (runsvdir) sends SIGTERM to supervisors
whose directories disappeared.

supervisor: a tool which monitors one service directory.
It runs "run" script as its child. It restarts it if it dies.
It can be instructed to start/stop/signal its child.
In daemontools package, it is called "supervise". In runit, it is called
"runsv". In s6, it is called "s6-supervise".

Conceptually, a daemontools clone can be designed such that it does not *have*
the supervisor component: service monitor can directly monitor all its daemons
(for example, this may be a good idea for memory-constrained systems).
However all three existing projects (daemontools/runit/s6) do have a per-service
supervisor process.

log service: a service which is exclusively tasked with logging
the output of another service. It is implemented as log/ subdirectory
in a service directory. It has the same structure as "normal"
service dirs: it has a "run" script which starts a logging tool.

If log service exists, stdout of its "main" service is piped
to log service. Stops/restarts of either of them do not sever the pipe
between them.

If log service exists, daemontools and s6 run a pair of supervisors
(one for the daemon, one for the logger); runit runs only one supervisor
per service, which is handling both of them (presumably this is done
to use fewer processes and thus, fewer resources).


	User API

"Users" of service monitoring are authors of software which has daemons.
They need to package their daemons to be installed as services at package
install time. And they need to do this for many distros.
The less distros diverge, the easier users' lives are.

System-wide service dirs reside in a distro-specific location.
The recommended location is /var/service. (However, since it is not
a mandatory location, avoid depending on it in your run scripts.
Void Linux wanted to have it somewhere in /run/*, and they solved this
by making /var/service a symlink).

The install location for service dirs is /etc/rc:
when e.g. ntpd daemon is installed, it creates the /etc/rc/ntpd
directory with (minimally) one executable file (script) named "run"
which starts ntpd daemon. It can have other files there.

At boot, distro should copy /etc/rc/* to a suitable writable
directory (common choice are /var/service, /run/service etc).
It should create log/ directories in each subdirectory
and create "run" files in them with suitable (for this particular distro)
logging tool invocation, unless this directory chose to channel
all logging from all daemons through service monitor process
and log all of them into one file/database/whatever,
in which case log/ directories should not be created.

It is allowable for a distro to directly use /etc/rc/ as the only
location of its service directories. (For example,
/var/service may be a symlink to /etc/rc).
However, it poses some problems:

(1) Supervision tools will need to write to subdirectories:
the control of running daemons is implemented via some files and fifos
in automatically created supervise/ subdirectory in each /etc/rc/DIR.

(2) Creation of a new service can race with the rescanning of /etc/rc/
by service monitor: service monitor may see a directory with only some files
present. If it attempts to start the service in this state, all sorts
of bad things may happen. This may be worked around by various
heuristics in service monitor which give new service a few seconds
of "grace time" to be fully populated; but this is not yet
implemented in any of three packages.
This also may be worked around by creating a .dotdir (a directory
whose name starts with a dot), populating it, and then renaming;
but packaging tools usually do not have an option to do this
automatically - additional install scripting in packages will be needed.

Daemons' output file descriptors are handled somewhat awkwardly
by various daemontools implementations. For example, for runit tools,
daemons' stdout goes to wherever runsvdir's stdout was directed;
stderr goes to runsvdir, which in turn "rotates" it on its command line
(which is visible in ps output).

Hopefully this get changed/standardized; while it is not, the "run" file
should start with a

    exec 2>&1

command, making stderr equivalent to stdout.
An especially primitive service which does not want its output to be logged
with standard tools can do

    exec >LOGFILE 2>&1

or even

    exec >/dev/null 2>&1

To prevent creation of distro-specific log/ directory, a service directory
in /etc/rc can contain an empty "log" file.


	Controlling daemons

The "svc" tool is available for admins and scripts to control services.
In particular, often one service needs to control another:
e.g. ifplugd can detect that the network cable was just plugged in,
and it needs to (re)start DHCP service for this network device.

The name of this tool is not standard either, which is an obvious problem.
I propose to fix this by implementing a tool with fixed name and API by all
daemontools clones. Lets use original daemontools name and API. Thus:

The following form must work:

	svc -udopchaitkx DIR

Options map to up/down/once/STOP/CONT/HUP/ALRM/INT/TERM/KILL/exit
commands to the daemon being controlled.

The form with one option letter must work. If multiple-option form
is supported, there is no guarantee in which order they take effect:
svc -it DIR can deliver TERM and INT in any order.

If more than one DIR can be specified (which is not a requirement),
there is no guarantee in which order commands are sent to them.

If DIR has no slash and is not "." or "..", it is assumed to be
relative to the system-wide service directory.

[Currently, "svc" exists only in daemontools and in busybox.
This proposal asks developers of other daemontools implementations
to add "svc" command to their projects]

The "svok DIR" tool exits 0 if service supervisor is running
(with service itself either running or stopped), and nonzero if not.

Other tools with different names and APIs may exist; however
for portability scripts should use the above tools.

Creation of a new service on a running system should be done atomically.
To this end, first create and populate a new /etc/rc/DIR.

Then "activate" it by running ??????? - this copies (or symlinks,
depending on the distro) its files to the "live" service directory,
wherever it is located on this distro.

Removal of the service should be done as follows:
svc -d DIR [DIR/log], then remove the service directory:
this makes service monitor SIGTERM per-directory supervisors
(if they exist in the implementation).


	Implementation details

Top-level service monitor program name is not standardized
[svscan, runsvdir, s6-svscan ...] - it does not need to be,
as far as daemon packagers are concerned.

It may run one per-directory supervisor, or two supervisors
(one for DIR/ and one for DIR/log/); for memory-constrained systems
an implementation is possible which itself controls all services, without
intermediate supervisors.
[runsvdir runs one "runsv DIR" per DIR, runsv handles DIR/log/ if that exists]
[svscan runs a pair of "supervise DIR" and "supervise DIR/log"]

Directories are remembered by device+inode numbers, not names. Renaming a directory
does not affect the running service (unless it is renamed to a .dotdir).

Removal (or .dotdiring) of a directory sends SIGTERM to any running services.

Standard output of non-logged services goes to standard output of service monitor.
Standard output of logger services goes to standard output of service monitor.
Standard error of them always goes to standard error of service monitor.

If you want to log standard error of your logged service along with its stdout, use
"exec 2>&1" in the beginning of your "run" script.

Whether stdout/stderr of service monitor is discarded (>/dev/null)
or logged in some way is system-dependent.


	Containers

[What do containers need?]
