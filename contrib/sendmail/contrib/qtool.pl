#!/usr/bin/env perl
##
## Copyright (c) 1998-2002 Proofpoint, Inc. and its suppliers.
##	All rights reserved.
##
## $Id: qtool.pl,v 8.32 2013-11-22 20:51:18 ca Exp $
##
use strict;
use File::Basename;
use File::Copy;
use File::Spec;
use Fcntl qw(:flock :DEFAULT);
use Getopt::Std;

##
## QTOOL
##	This program is for moving files between sendmail queues. It is
## pretty similar to just moving the files manually, but it locks the files
## the same way sendmail does to prevent problems. 
##
##	NOTICE: Do not use this program to move queue files around
## if you use sendmail 8.12 and multiple queue groups. It may interfere
## with sendmail's internal queue group selection strategy and can cause
## mail to be not delivered.
##
## 	The syntax is the reverse of mv (ie. the target argument comes
## first). This lets you pick the files you want to move using find and
## xargs.
##
## 	Since you cannot delete queues while sendmail is running, QTOOL
## assumes that when you specify a directory as a source, you mean that you
## want all of the queue files within that directory moved, not the 
## directory itself.
##
##	There is a mechanism for adding conditionals for moving the files.
## Just create an Object with a check_move(source, dest) method and add it 
## to the $conditions object. See the handling of the '-s' option for an
## example.
##

##
## OPTION NOTES
##
## The -e option:
##	The -e option takes any valid perl expression and evaluates it
##	using the eval() function. Inside the expression the variable 
##	'$msg' is bound to the ControlFile object for the current source
##	queue message. This lets you check for any value in the message
##	headers or the control file. Here's an example:
##
##	./qtool.pl -e '$msg{num_delivery_attempts} >= 2' /q1 /q2
##
##	This would move any queue files whose number of delivery attempts
##	is greater than or equal to 2 from the queue 'q2' to the queue 'q1'.
##
##	See the function ControlFile::parse for a list of available
##	variables.
##

my %opts;
my %sources;
my $dst_name;
my $destination;
my $source_name;
my $source;
my $result;
my $action;
my $new_condition;
my $qprefix;
my $queuegroups = 0;
my $conditions = new Compound();
my $fcntl_struct = 's H60';
my $fcntl_structlockp = pack($fcntl_struct, Fcntl::F_WRLCK,
	"000000000000000000000000000000000000000000000000000000000000");
my $fcntl_structunlockp = pack($fcntl_struct, Fcntl::F_UNLCK,
	"000000000000000000000000000000000000000000000000000000000000");
my $lock_both = -1;

Getopt::Std::getopts('bC:de:Qs:', \%opts);

sub move_action
{
	my $source = shift;
	my $destination = shift;

	$result = $destination->add($source);
	if ($result)
	{
		print("$result.\n");
	}
}

sub delete_action
{
	my $source = shift;

	return $source->delete();
}

sub bounce_action
{
	my $source = shift;

	return $source->bounce();
}

$action = \&move_action;
if (defined $opts{d})
{
	$action = \&delete_action;
}
elsif (defined $opts{b})
{
	$action = \&bounce_action;
}

if (defined $opts{s})
{
	$new_condition = new OlderThan($opts{s});
	$conditions->add($new_condition);
}

if (defined $opts{e})
{
	$new_condition = new Eval($opts{e});
	$conditions->add($new_condition);
}

if (defined $opts{Q})
{
	$qprefix = "hf";
}
else
{
	$qprefix = "qf";
}

if ($action == \&move_action)
{
	$dst_name = shift(@ARGV);
	if (!-d $dst_name)
	{
		print("The destination '$dst_name' must be an existing " .
		      "directory.\n");
		usage();
		exit;
	}
	$destination = new Queue($dst_name);
}

# determine queue_root by reading config file
my $queue_root;
{
	my $config_file = "/etc/mail/sendmail.cf";
	if (defined $opts{C})
	{
		$config_file = $opts{C};
	}

	my $line;
	open(CONFIG_FILE, $config_file) or die "$config_file: $!";

	##  Notice: we can only break out of this loop (using last)
	##	when both entries (queue directory and group group)
	##	have been found.
	while ($line = <CONFIG_FILE>)
	{
		chomp $line;
		if ($line =~ m/^O QueueDirectory=(.*)/)
		{
			$queue_root = $1;
			if ($queue_root =~ m/(.*)\/[^\/]+\*$/)
			{
				$queue_root = $1;
			}
			# found also queue groups?
			if ($queuegroups)
			{
				last;
			}
		}
		if ($line =~ m/^Q.*/)
		{
			$queuegroups = 1;
			if ($action == \&move_action)
			{
				print("WARNING: moving queue files around " .
				      "when queue groups are used may\n" .
				      "result in undelivered mail!\n");
			}
			# found also queue directory?
			if (defined $queue_root)
			{
				last;
			}
		}
	}
	close(CONFIG_FILE);
	if (!defined $queue_root)
	{
		die "QueueDirectory option not defined in $config_file";
	}
}

while (@ARGV)
{
	$source_name = shift(@ARGV);
	$result = add_source(\%sources, $source_name);
	if ($result)
	{
		print("$result.\n");
		exit;
	}
}

if (keys(%sources) == 0)
{
	exit;
}

while (($source_name, $source) = each(%sources))
{
	$result = $conditions->check_move($source, $destination);
	if ($result)
	{
		$result = &{$action}($source, $destination);
		if ($result)
		{
			print("$result\n");
		}
	}
}

sub usage
{
	print("Usage:\t$0 [options] directory source ...\n");
	print("\t$0 [-Q][-d|-b] source ...\n");
	print("Options:\n");
	print("\t-b\t\tBounce the messages specified by source.\n");
	print("\t-C configfile\tSpecify sendmail config file.\n");
	print("\t-d\t\tDelete the messages specified by source.\n");
	print("\t-e [perl expression]\n");
	print("\t\t\tMove only messages for which perl expression\n");
	print("\t\t\treturns true.\n");
	print("\t-Q\t\tOperate on quarantined files.\n");
	print("\t-s [seconds]\tMove only messages whose queue file is older\n");
	print("\t\t\tthan seconds.\n");
}

##
## ADD_SOURCE -- Adds a source to the source hash.
##
##	Determines whether source is a file, directory, or id. Then it 
##	creates a QueuedMessage or Queue for that source and adds it to the
##	list.
##
##	Parameters:
##		sources -- A hash that contains all of the sources.
##		source_name -- The name of the source to add
##
##	Returns:
##		error_string -- Undef if ok. Error string otherwise.
##
##	Notes:
##		If a new source comes in with the same ID as a previous 
##		source, the previous source gets overwritten in the sources
##		hash. This lets the user specify things like * and it still
##		works nicely.
##

sub add_source
{
	my $sources = shift;
	my $source_name = shift;
	my $source_base_name;
	my $source_dir_name;
	my $data_dir_name;
	my $source_id;
	my $source_prefix;
	my $queued_message;
	my $queue;
	my $result;

	($source_base_name, $source_dir_name) = File::Basename::fileparse($source_name);
	$data_dir_name = $source_dir_name;

	$source_prefix = substr($source_base_name, 0, 2);
	if (!-d $source_name && $source_prefix ne $qprefix && 
	    $source_prefix ne 'df')
	{
		$source_base_name = "$qprefix$source_base_name";
		$source_name = File::Spec->catfile("$source_dir_name", 
						   "$source_base_name");
	}
	$source_id = substr($source_base_name, 2);

	if (!-e $source_name)
	{
		$source_name = File::Spec->catfile("$source_dir_name", "qf",
						   "$qprefix$source_id");
		if (!-e $source_name)
		{
			return "'$source_name' does not exist";
		}
		$data_dir_name = File::Spec->catfile("$source_dir_name", "df");
		if (!-d $data_dir_name)
		{
			$data_dir_name = $source_dir_name;
		}
		$source_dir_name = File::Spec->catfile("$source_dir_name", 
						       "qf");
	}

	if (-f $source_name)
	{
		$queued_message = new QueuedMessage($source_dir_name, 
						    $source_id,
						    $data_dir_name);
		$sources->{$source_id} = $queued_message;
		return undef;
	}

	if (!-d $source_name)
	{
		return "'$source_name' is not a plain file or a directory";
	}

	$queue = new Queue($source_name);
	$result = $queue->read();
	if ($result)
	{
		return $result;
	}

	while (($source_id, $queued_message) = each(%{$queue->{files}}))
	{
		$sources->{$source_id} = $queued_message;
	}

	return undef;
}

##
## LOCK_FILE -- Opens and then locks a file.
##
## 	Opens a file for read/write and uses flock to obtain a lock on the
##	file. The flock is Perl's flock which defaults to flock on systems
##	that support it. On systems without flock it falls back to fcntl
##	locking.  This script will also call fcntl explicitly if flock
##      uses BSD semantics (i.e. if both flock() and fcntl() can successfully
##      lock the file at the same time)
##
##	Parameters:
##		file_name -- The name of the file to open and lock.
##
##	Returns:
##		(file_handle, error_string) -- If everything works then
##			file_handle is a reference to a file handle and
##			error_string is undef. If there is a problem then 
##			file_handle is undef and error_string is a string
##			explaining the problem.
##

sub lock_file
{
	my $file_name = shift;
	my $result;

	if ($lock_both == -1)
	{
		if (open(DEVNULL, '>/dev/null'))
		{
			my $flock_status = flock(DEVNULL, Fcntl::LOCK_EX | Fcntl::LOCK_NB);
			my $fcntl_status = fcntl (DEVNULL, Fcntl::F_SETLK, $fcntl_structlockp);
			close(DEVNULL);

			$lock_both = ($flock_status && $fcntl_status);
		}
		else
		{
			# Couldn't open /dev/null.  Windows system?
			$lock_both = 0;
		}
	}


	$result = sysopen(FILE_TO_LOCK, $file_name, Fcntl::O_RDWR);
	if (!$result)
	{
		return (undef, "Unable to open '$file_name': $!");
	}

	$result = flock(FILE_TO_LOCK, Fcntl::LOCK_EX | Fcntl::LOCK_NB);
	if (!$result)
	{
		return (undef, "Could not obtain lock on '$file_name': $!");
	}

	if ($lock_both)
	{
		my $result2 = fcntl (FILE_TO_LOCK, Fcntl::F_SETLK, $fcntl_structlockp);
		if (!$result2)
		{
			return (undef, "Could not obtain fcntl lock on '$file_name': $!");
		}
	}

	return (\*FILE_TO_LOCK, undef);
}

##
## UNLOCK_FILE -- Unlocks a file.
##
## 	Unlocks a file using Perl's flock.
##
##	Parameters:
##		file -- A file handle.
##
##	Returns:
##		error_string -- If undef then no problem. Otherwise it is a 
##			string that explains problem.
##

sub unlock_file
{
	my $file = shift;
	my $result;

	$result = flock($file, Fcntl::LOCK_UN);
	if (!$result)
	{
		return "Unlock failed on '$result': $!";
	}
	if ($lock_both)
	{
		my $result2 = fcntl ($file, Fcntl::F_SETLK, $fcntl_structunlockp);
		if (!$result2)
		{
			return (undef, "Fcntl unlock failed on '$result': $!");
		}
	}

	return undef;
}

##
## MOVE_FILE -- Moves a file.
##
##	Moves a file.
##
##	Parameters:
##		src_name -- The name of the file to be move.
##		dst_name -- The name of the place to move it to.
##
##	Returns:
##		error_string -- If undef then no problem. Otherwise it is a 
##			string that explains problem.
##

sub move_file
{
	my $src_name = shift;
	my $dst_name = shift;
	my $result;

	$result = File::Copy::move($src_name, $dst_name);
	if (!$result)
	{
		return "File move from '$src_name' to '$dst_name' failed: $!";
	}

	return undef;
}


##
## CONTROL_FILE - Represents a sendmail queue control file.
##
##	This object represents represents a sendmail queue control file.
##	It can parse and lock its file.
##


package ControlFile;

sub new
{
	my $this = shift;
	my $class = ref($this) || $this;
	my $self = {};
	bless $self, $class;
	$self->initialize(@_);
	return $self;
}

sub initialize
{
	my $self = shift;
	my $queue_dir = shift;
	$self->{id} = shift;

	$self->{file_name} = $queue_dir . '/' . $qprefix . $self->{id};
	$self->{headers} = {};
}

##
## PARSE - Parses the control file.
##
##	Parses the control file. It just sticks each entry into a hash.
##	If a key has more than one entry, then it points to a list of
##	entries.
##

sub parse
{
	my $self = shift;
	if ($self->{parsed})
	{
		return;
	}
	my %parse_table = 
	(
		'A' => 'auth',
		'B' => 'body_type',
		'C' => 'controlling_user',
		'D' => 'data_file_name',
		'd' => 'data_file_directory',
		'E' => 'error_recipient',
		'F' => 'flags',
		'H' => 'parse_header',
		'I' => 'inode_number',
		'K' => 'next_delivery_time',
		'L' => 'content-length',
		'M' => 'message',
		'N' => 'num_delivery_attempts',
		'P' => 'priority',
		'Q' => 'original_recipient',
		'R' => 'recipient',
		'q' => 'quarantine_reason',
		'r' => 'final_recipient',
		'S' => 'sender',
		'T' => 'creation_time',
		'V' => 'version',
		'Y' => 'current_delay',
		'Z' => 'envid',
		'!' => 'deliver_by',
		'$' => 'macro'
	);
	my $line;
	my $line_type;
	my $line_value;
	my $member_name;
	my $member;
	my $last_type;

	open(CONTROL_FILE, "$self->{file_name}");
	while ($line = <CONTROL_FILE>)
	{
		$line_type = substr($line, 0, 1);
		if ($line_type eq "\t" && $last_type eq 'H')
		{
			$line_type = 'H';
			$line_value = $line;
		}
		else
		{
			$line_value = substr($line, 1);
		}
		$member_name = $parse_table{$line_type};
		$last_type = $line_type;
		if (!$member_name)
		{
			$member_name = 'unknown';
		}
		if ($self->can($member_name))
		{
			$self->$member_name($line_value);
		}
		$member = $self->{$member_name};
		if (!$member)
		{
			$self->{$member_name} = $line_value;
			next;
		}
		if (ref($member) eq 'ARRAY')
		{
			push(@{$member}, $line_value);
			next;
		}
		$self->{$member_name} = [$member, $line_value];
	}
	close(CONTROL_FILE);

	$self->{parsed} = 1;
}

sub parse_header
{
	my $self = shift;
	my $line = shift;
	my $headers = $self->{headers};
	my $last_header = $self->{last_header};
	my $header_name;
	my $header_value;
	my $first_char;

	$first_char = substr($line, 0, 1);
	if ($first_char eq "?")
	{
		$line = (split(/\?/, $line,3))[2];
	}
	elsif ($first_char eq "\t")
	{
	 	if (ref($headers->{$last_header}) eq 'ARRAY')
		{
			$headers->{$last_header}[-1] = 
				$headers->{$last_header}[-1] . $line;
		}
		else
		{
			$headers->{$last_header} = $headers->{$last_header} . 
						   $line;
		}
		return;
	}
	($header_name, $header_value) = split(/:/, $line, 2);
	$self->{last_header} = $header_name;
	if (exists $headers->{$header_name})
	{
		$headers->{$header_name} = [$headers->{$header_name}, 
					    $header_value];
	}
	else
	{
		$headers->{$header_name} = $header_value;
	}
}

sub is_locked
{
	my $self = shift;

	return (defined $self->{lock_handle});
}

sub lock
{
	my $self = shift;
	my $lock_handle;
	my $result;

	if ($self->is_locked())
	{
		# Already locked
		return undef;
	}

	($lock_handle, $result) = ::lock_file($self->{file_name});
	if (!$lock_handle)
	{
		return $result;
	}

	$self->{lock_handle} = $lock_handle;

	return undef;
}

sub unlock
{
	my $self = shift;
	my $result;

	if (!$self->is_locked())
	{
		# Not locked
		return undef;
	}

	$result = ::unlock_file($self->{lock_handle});

	$self->{lock_handle} = undef;

	return $result;
}

sub do_stat
{
	my $self = shift;
	my $result;
	my @result;

	$result = open(QUEUE_FILE, $self->{file_name});
	if (!$result)
	{
		return "Unable to open '$self->{file_name}': $!";
	}
	@result = stat(QUEUE_FILE);
	if (!@result)
	{
		return "Unable to stat '$self->{file_name}': $!";
	}
	$self->{control_size} = $result[7];
	$self->{control_last_mod_time} = $result[9];
}

sub DESTROY
{
	my $self = shift;

	$self->unlock();
}

sub delete
{
	my $self = shift;
	my $result;

	$result = unlink($self->{file_name});
	if (!$result)
	{
		return "Unable to delete $self->{file_name}: $!";
	}
	return undef;
}


##
## DATA_FILE - Represents a sendmail queue data file.
##
##	This object represents represents a sendmail queue data file.
##	It is really just a place-holder.
##

package DataFile;

sub new
{
	my $this = shift;
	my $class = ref($this) || $this;
	my $self = {};
	bless $self, $class;
	$self->initialize(@_);
	return $self;
}

sub initialize
{
	my $self = shift;
	my $data_dir = shift;
	$self->{id} = shift;
	my $control_file = shift;

	$self->{file_name} = $data_dir . '/df' . $self->{id};
	return if -e $self->{file_name};
	$control_file->parse();
	return if !defined $control_file->{data_file_directory};
	$data_dir = $queue_root . '/' . $control_file->{data_file_directory};
	chomp $data_dir;
	if (-d ($data_dir . '/df'))
	{
		$data_dir .= '/df';
	}
	$self->{file_name} = $data_dir . '/df' . $self->{id};
}

sub do_stat
{
	my $self = shift;
	my $result;
	my @result;

	$result = open(QUEUE_FILE, $self->{file_name});
	if (!$result)
	{
		return "Unable to open '$self->{file_name}': $!";
	}
	@result = stat(QUEUE_FILE);
	if (!@result)
	{
		return "Unable to stat '$self->{file_name}': $!";
	}
	$self->{body_size} = $result[7];
	$self->{body_last_mod_time} = $result[9];
}

sub delete
{
	my $self = shift;
	my $result;

	$result = unlink($self->{file_name});
	if (!$result)
	{
		return "Unable to delete $self->{file_name}: $!";
	}
	return undef;
}


##
## QUEUED_MESSAGE - Represents a queued sendmail message.
##
##	This keeps track of the files that make up a queued sendmail 
##	message.
##	Currently it has 'control_file' and 'data_file' as members.
##
##	You can tie it to a fetch only hash using tie. You need to
##	pass a reference to a QueuedMessage as the third argument
##	to tie.
##

package QueuedMessage;

sub new
{
	my $this = shift;
	my $class = ref($this) || $this;
	my $self = {};
	bless $self, $class;
	$self->initialize(@_);
	return $self;
}

sub initialize
{
	my $self = shift;
	my $queue_dir = shift;
	my $id = shift;
	my $data_dir = shift;

	$self->{id} = $id;
	$self->{control_file} = new ControlFile($queue_dir, $id);
	if (!$data_dir)
	{
		$data_dir = $queue_dir;
	}
	$self->{data_file} = new DataFile($data_dir, $id, $self->{control_file});
}

sub last_modified_time
{
	my $self = shift;
	my @result;
	@result = stat($self->{data_file}->{file_name});
	return $result[9];
}

sub TIEHASH
{
	my $this = shift;
	my $class = ref($this) || $this;
	my $self = shift;
	return $self;
}

sub FETCH
{
	my $self = shift;
	my $key = shift;

	if (exists $self->{control_file}->{$key})
	{
		return $self->{control_file}->{$key};
	}
	if (exists $self->{data_file}->{$key})
	{
		return $self->{data_file}->{$key};
	}

	return undef;
}

sub lock
{
	my $self = shift;

	return $self->{control_file}->lock();
}

sub unlock
{
	my $self = shift;

	return $self->{control_file}->unlock();
}

sub move
{
	my $self = shift;
	my $destination = shift;
	my $df_dest;
	my $qf_dest;
	my $result;

	$result = $self->lock();
	if ($result)
	{
		return $result;
	}

	$qf_dest = File::Spec->catfile($destination, "qf");
	if (-d $qf_dest)
	{
		$df_dest = File::Spec->catfile($destination, "df");
		if (!-d $df_dest)
		{
			$df_dest = $destination;
		}
	}
	else
	{
		$qf_dest = $destination;
		$df_dest = $destination;
	}

	if (-e File::Spec->catfile($qf_dest, "$qprefix$self->{id}"))
	{
		$result = "There is already a queued message with id '$self->{id}' in '$destination'";
	}

	if (!$result)
	{
		$result = ::move_file($self->{data_file}->{file_name}, 
				      $df_dest);
	}

	if (!$result)
	{
		$result = ::move_file($self->{control_file}->{file_name}, 
				      $qf_dest);
	}

	$self->unlock();

	return $result;
}

sub parse
{
	my $self = shift;

	return $self->{control_file}->parse();
}

sub do_stat
{
	my $self = shift;

	$self->{control_file}->do_stat();
	$self->{data_file}->do_stat();
}

sub setup_vars
{
	my $self = shift;

	$self->parse();
	$self->do_stat();
}

sub delete
{
	my $self = shift;
	my $result;

	$result = $self->{control_file}->delete();
	if ($result)
	{
		return $result;
	}
	$result = $self->{data_file}->delete();
	if ($result)
	{
		return $result;
	}

	return undef;
}

sub bounce
{
	my $self = shift;
	my $command;

	$command = "sendmail -qI$self->{id} -O Timeout.queuereturn=now";
#	print("$command\n");
	system($command);
}

##
## QUEUE - Represents a queued sendmail queue.
##
##	This manages all of the messages in a queue.
##

package Queue;

sub new
{
	my $this = shift;
	my $class = ref($this) || $this;
	my $self = {};
	bless $self, $class;
	$self->initialize(@_);
	return $self;
}

sub initialize
{
	my $self = shift;

	$self->{queue_dir} = shift;
	$self->{files} = {};
}

##
## READ - Loads the queue with all of the objects that reside in it.
##
##	This reads the queue's directory and creates QueuedMessage objects
## 	for every file in the queue that starts with 'qf' or 'hf'
##	(depending on the -Q option).
##

sub read
{
	my $self = shift;
	my @control_files;
	my $queued_message;
	my $file_name;
	my $id;
	my $result;
	my $control_dir;
	my $data_dir;

	$control_dir = File::Spec->catfile($self->{queue_dir}, 'qf');

	if (-e $control_dir)
	{
		$data_dir = File::Spec->catfile($self->{queue_dir}, 'df');
		if (!-e $data_dir)
		{
			$data_dir = $self->{queue_dir};
		}
	}
	else
	{
		$data_dir = $self->{queue_dir};
		$control_dir = $self->{queue_dir};
	}

	$result = opendir(QUEUE_DIR, $control_dir);
	if (!$result)
	{
		return "Unable to open directory '$control_dir'";
	}

	@control_files = grep { /^$qprefix.*/ && -f "$control_dir/$_" } readdir(QUEUE_DIR);
	closedir(QUEUE_DIR);
	foreach $file_name (@control_files)
	{
		$id = substr($file_name, 2);
		$queued_message = new QueuedMessage($control_dir, $id, 
						    $data_dir);
		$self->{files}->{$id} = $queued_message;
	}

	return undef;
}


##
## ADD_QUEUED_MESSAGE - Adds a QueuedMessage to this Queue.
##
##	Adds the QueuedMessage object to the hash and moves the files
##	associated with the QueuedMessage to this Queue's directory.
##

sub add_queued_message
{
	my $self = shift;
	my $queued_message = shift;
	my $result;

	$result = $queued_message->move($self->{queue_dir});
	if ($result)
	{
		return $result;
	}

	$self->{files}->{$queued_message->{id}} = $queued_message;

	return $result;
}

##
## ADD_QUEUE - Adds another Queue's QueuedMessages to this Queue.
##
##	Adds all of the QueuedMessage objects in the passed in queue
##	to this queue.
##

sub add_queue
{
	my $self = shift;
	my $queue = shift;
	my $id;
	my $queued_message;
	my $result;

	while (($id, $queued_message) = each %{$queue->{files}})
	{
		$result = $self->add_queued_message($queued_message);
		if ($result)
		{
			print("$result.\n");
		}
	}
}

##
## ADD - Adds an item to this queue.
##
##	Adds either a Queue or a QueuedMessage to this Queue.
##

sub add
{
	my $self = shift;
	my $source = shift;
	my $type_name;
	my $result;

	$type_name = ref($source);

	if ($type_name eq "QueuedMessage")
	{
		return $self->add_queued_message($source);
	}

	if ($type_name eq "Queue")
	{
		return $self->add_queue($source);
	}

	return "Queue does not know how to add a '$type_name'"
}

sub delete
{
	my $self = shift;
	my $id;
	my $queued_message;

	while (($id, $queued_message) = each %{$self->{files}})
	{
		$result = $queued_message->delete();
		if ($result)
		{
			print("$result.\n");
		}
	}
}

sub bounce
{
	my $self = shift;
	my $id;
	my $queued_message;

	while (($id, $queued_message) = each %{$self->{files}})
	{
		$result = $queued_message->bounce();
		if ($result)
		{
			print("$result.\n");
		}
	}
}

##
## Condition Class
##
## 	This next section is for any class that has an interface called 
##	check_move(source, dest). Each class represents some condition to
##	check for to determine whether we should move the file from 
##	source to dest.
##


##
## OlderThan
##
##	This Condition Class checks the modification time of the
##	source file and returns true if the file's modification time is
##	older than the number of seconds the class was initialized with.
##

package OlderThan;

sub new
{
	my $this = shift;
	my $class = ref($this) || $this;
	my $self = {};
	bless $self, $class;
	$self->initialize(@_);
	return $self;
}

sub initialize
{
	my $self = shift;

	$self->{age_in_seconds} = shift;
}

sub check_move
{
	my $self = shift;
	my $source = shift;

	if ((time() - $source->last_modified_time()) > $self->{age_in_seconds})
	{
		return 1;
	}

	return 0;
}

##
## Compound
##
##	Takes a list of Move Condition Classes. Check_move returns true
##	if every Condition Class in the list's check_move function returns
##	true.
##

package Compound;

sub new
{
	my $this = shift;
	my $class = ref($this) || $this;
	my $self = {};
	bless $self, $class;
	$self->initialize(@_);
	return $self;
}

sub initialize
{
	my $self = shift;

	$self->{condition_list} = [];
}

sub add
{
	my $self = shift;
	my $new_condition = shift;

	push(@{$self->{condition_list}}, $new_condition);
}

sub check_move
{
	my $self = shift;
	my $source = shift;
	my $dest = shift;
	my $condition;
	my $result;

	foreach $condition (@{$self->{condition_list}})
	{
		if (!$condition->check_move($source, $dest))
		{
			return 0;
		}
	}
	
	return 1;
}

##
## Eval
##
##	Takes a perl expression and evaluates it. The ControlFile object
##	for the source QueuedMessage is available through the name '$msg'.
##

package Eval;

sub new
{
	my $this = shift;
	my $class = ref($this) || $this;
	my $self = {};
	bless $self, $class;
	$self->initialize(@_);
	return $self;
}

sub initialize
{
	my $self = shift;

	$self->{expression} = shift;
}

sub check_move
{
	my $self = shift;
	my $source = shift;
	my $dest = shift;
	my $result;
	my %msg;

	$source->setup_vars();
	tie(%msg, 'QueuedMessage', $source);
	$result = eval($self->{expression});

	return $result;
}
