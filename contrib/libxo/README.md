libxo
=====

libxo - A Library for Generating Text, XML, JSON, and HTML Output

The libxo library allows an application to generate text, XML, JSON,
and HTML output using a common set of function calls.  The application
decides at run time which output style should be produced.  The
application calls a function "xo_emit" to product output that is
described in a format string.  A "field descriptor" tells libxo what
the field is and what it means.

```
    xo_emit(" {:lines/%7ju/%ju} {:words/%7ju/%ju} "
            "{:characters/%7ju/%ju}{d:filename/%s}\n",
            linect, wordct, charct, file);
```

Output can then be generated in various style, using the "--libxo"
option: 

```
    % wc /etc/motd
          25     165    1140 /etc/motd
    % wc --libxo xml,pretty,warn /etc/motd
    <wc>
      <file>
        <filename>/etc/motd</filename>
        <lines>25</lines>
        <words>165</words>
        <characters>1140</characters>
      </file>
    </wc>
    % wc --libxo json,pretty,warn /etc/motd
    {
      "wc": {
        "file": [
          {
            "filename": "/etc/motd",
            "lines": 25,
            "words": 165,
            "characters": 1140
          }
        ]
      }
    }
    % wc --libxo html,pretty,warn /etc/motd
    <div class="line">
      <div class="text"> </div>
      <div class="data" data-tag="lines">     25</div>
      <div class="text"> </div>
      <div class="data" data-tag="words">    165</div>
      <div class="text"> </div>
      <div class="data" data-tag="characters">   1140</div>
      <div class="text"> </div>
      <div class="data" data-tag="filename">/etc/motd</div>
    </div>
```

View the beautiful documentation at:

http://juniper.github.io/libxo/libxo-manual.html

[![Analytics](https://ga-beacon.appspot.com/UA-56056421-1/Juniper/libxo/Readme)](https://github.com/Juniper/libxo)
