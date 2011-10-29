<refentry id="vidioc-enumaudioout">
  <refmeta>
    <refentrytitle>ioctl VIDIOC_ENUMAUDOUT</refentrytitle>
    &manvol;
  </refmeta>

  <refnamediv>
    <refname>VIDIOC_ENUMAUDOUT</refname>
    <refpurpose>Enumerate audio outputs</refpurpose>
  </refnamediv>

  <refsynopsisdiv>
    <funcsynopsis>
      <funcprototype>
	<funcdef>int <function>ioctl</function></funcdef>
	<paramdef>int <parameter>fd</parameter></paramdef>
	<paramdef>int <parameter>request</parameter></paramdef>
	<paramdef>struct v4l2_audioout *<parameter>argp</parameter></paramdef>
      </funcprototype>
    </funcsynopsis>
  </refsynopsisdiv>

  <refsect1>
    <title>Arguments</title>

    <variablelist>
      <varlistentry>
	<term><parameter>fd</parameter></term>
	<listitem>
	  <para>&fd;</para>
	</listitem>
      </varlistentry>
      <varlistentry>
	<term><parameter>request</parameter></term>
	<listitem>
	  <para>VIDIOC_ENUMAUDOUT</para>
	</listitem>
      </varlistentry>
      <varlistentry>
	<term><parameter>argp</parameter></term>
	<listitem>
	  <para></para>
	</listitem>
      </varlistentry>
    </variablelist>
  </refsect1>

  <refsect1>
    <title>Description</title>

    <para>To query the attributes of an audio output applications
initialize the <structfield>index</structfield> field and zero out the
<structfield>reserved</structfield> array of a &v4l2-audioout; and
call the <constant>VIDIOC_G_AUDOUT</constant> ioctl with a pointer
to this structure. Drivers fill the rest of the structure or return an
&EINVAL; when the index is out of bounds. To enumerate all audio
outputs applications shall begin at index zero, incrementing by one
until the driver returns <errorcode>EINVAL</errorcode>.</para>

    <para>Note connectors on a TV card to loop back the received audio
signal to a sound card are not audio outputs in this sense.</para>

    <para>See <xref linkend="vidioc-g-audioout" /> for a description of
&v4l2-audioout;.</para>
  </refsect1>

  <refsect1>
    &return-value;

    <variablelist>
      <varlistentry>
	<term><errorcode>EINVAL</errorcode></term>
	<listitem>
	  <para>The number of the audio output is out of bounds.</para>
	</listitem>
      </varlistentry>
    </variablelist>
  </refsect1>
</refentry>
