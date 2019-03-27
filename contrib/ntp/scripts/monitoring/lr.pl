;#
;# lr.pl,v 3.1 1993/07/06 01:09:08 jbj Exp
;#
;#
;# Linear Regression Package for perl
;# to be 'required' from perl
;#
;#  Copyright (c) 1992 
;#  Frank Kardel, Rainer Pruy
;#  Friedrich-Alexander Universitaet Erlangen-Nuernberg
;#
;#  Copyright (c) 1997 by
;#  Ulrich Windl <Ulrich.Windl@rz.uni-regensburg.de>
;#  (Converted to a PERL 5.004 package)
;#
;#############################################################

package lr;

##
## y = A + Bx
##
## B = (n * Sum(xy) - Sum(x) * Sum(y)) / (n * Sum(x^2) - Sum(x)^2)
##
## A = (Sum(y) - B * Sum(x)) / n
##

##
## interface
##
;# init(tag);		initialize data set for tag
;# sample(x, y, tag);	enter sample
;# Y(x, tag);		compute y for given x 
;# X(y, tag);		compute x for given y
;# r(tag);		regression coefficient
;# cov(tag);		covariance
;# A(tag);   
;# B(tag);
;# sigma(tag);		standard deviation
;# mean(tag);
#########################

sub init
{
    my $self = shift;

    $self->{n}   = 0;
    $self->{sx}  = 0.0;
    $self->{sx2} = 0.0;
    $self->{sxy} = 0.0;
    $self->{sy}  = 0.0;
    $self->{sy2} = 0.0;
}

sub sample($$)
{
    my $self = shift;
    my($_x, $_y) = @_;

    ++($self->{n});
    $self->{sx}  += $_x;
    $self->{sy}  += $_y;
    $self->{sxy} += $_x * $_y;
    $self->{sx2} += $_x**2;
    $self->{sy2} += $_y**2;
}

sub B()
{
    my $self = shift;

    return 1 unless ($self->{n} * $self->{sx2} - $self->{sx}**2);
    return ($self->{n} * $self->{sxy} - $self->{sx} * $self->{sy})
	/ ($self->{n} * $self->{sx2} - $self->{sx}**2);
}

sub A()
{
    my $self = shift;

    return ($self->{sy} - B() * $self->{sx}) / $self->{n};
}

sub Y()
{
    my $self = shift;

    return A() + B() * $_[$[];
}

sub X()
{
    my $self = shift;

    return ($_[$[] - A()) / B();
}

sub r()
{
    my $self = shift;

    my $s = ($self->{n} * $self->{sx2} - $self->{sx}**2)
	  * ($self->{n} * $self->{sy2} - $self->{sy}**2);

    return 1 unless $s;
    
    return ($self->{n} * $self->{sxy} - $self->{sx} * $self->{sy}) / sqrt($s);
}

sub cov()
{
    my $self = shift;

    return ($self->{sxy} - $self->{sx} * $self->{sy} / $self->{n})
	/ ($self->{n} - 1);
}

sub sigma()
{
    my $self = shift;

    return 0 if $self->{n} <= 1;
    return sqrt(($self->{sy2} - ($self->{sy} * $self->{sy}) / $self->{n})
		/ ($self->{n}));
}

sub mean()
{
    my $self = shift;

    return 0 if $self->{n} <= 0;
    return $self->{sy} / $self->{n};
}

sub new
{
    my $class = shift;
    my $self = {
	(n => undef,
	 sx => undef,
	 sx2 => undef,
	 sxy => undef,
	 sy => undef,
	 sy2 => undef)
    };
    bless $self, $class;
    init($self);
    return $self;
}

1;
