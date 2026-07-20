.. SPDX-License-Identifier: GPL-2.0
.. include:: ../disclaimer-sp.rst

:Original: Documentation/process/embargoed-hardware-issues.rst
:Translator: Avadhut Naik <avadhut.naik@amd.com>

Problemas de hardware embargados
================================

Alcance
-------

Los problemas de hardware que resultan en problemas de seguridad son una
categoría diferente de errores de seguridad que los errores de software
puro que solo afectan al kernel de Linux.

Los problemas de hardware como Meltdown, Spectre, L1TF, etc. deben
tratarse de manera diferente porque usualmente afectan a todos los
sistemas operativos (“OS”) y, por lo tanto, necesitan coordinación entre
vendedores diferentes de OS, distribuciones, vendedores de hardware y
otras partes. Para algunos de los problemas, las mitigaciones de software
pueden depender de actualizaciones de microcódigo o firmware, los cuales
necesitan una coordinación adicional.

.. _Contacto:

Contacto
--------

El equipo de seguridad de hardware del kernel de Linux es separado del
equipo regular de seguridad del kernel de Linux.

El equipo solo maneja la coordinación de los problemas de seguridad de
hardware embargados. Los informes de errores de seguridad de software puro
en el kernel de Linux no son manejados por este equipo y el "reportero"
(quien informa del error) será guiado a contactar el equipo de seguridad
del kernel de Linux (:doc:`errores de seguridad <security-bugs>`) en su
lugar.

El equipo puede contactar por correo electrónico en
<hardware-security@kernel.org>. Esta es una lista privada de oficiales de
seguridad que lo ayudarán a coordinar un problema de acuerdo con nuestro
proceso documentado.

La lista esta encriptada y el correo electrónico a la lista puede ser
enviado por PGP o S/MIME encriptado y debe estar firmado con la llave de
PGP del reportero o el certificado de S/MIME. La llave de PGP y el
certificado de S/MIME de la lista están disponibles en las siguientes
URLs:

  - PGP: https://www.kernel.org/static/files/hardware-security.asc
  - S/MIME: https://www.kernel.org/static/files/hardware-security.crt

Si bien los problemas de seguridad del hardware a menudo son manejados por
el vendedor de hardware afectado, damos la bienvenida al contacto de
investigadores o individuos que hayan identificado una posible falla de
hardware.

Oficiales de seguridad de hardware
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

El equipo actual de oficiales de seguridad de hardware:

  - Linus Torvalds (Linux Foundation Fellow)
  - Greg Kroah-Hartman (Linux Foundation Fellow)
  - Thomas Gleixner (Linux Foundation Fellow)

Operación de listas de correo
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Las listas de correo encriptadas que se utilizan en nuestro proceso están
alojados en la infraestructura de IT de la Fundación Linux. Al proporcionar
este servicio, los miembros del personal de operaciones de IT de la
Fundación Linux técnicamente tienen la capacidad de acceder a la
información embargada, pero están obligados a la confidencialidad por su
contrato de trabajo. El personal de IT de la Fundación Linux también es
responsable para operar y administrar el resto de la infraestructura de
kernel.org.

El actual director de infraestructura de proyecto de IT de la Fundación
Linux es Konstantin Ryabitsev.

Acuerdos de no divulgación
--------------------------

El equipo de seguridad de hardware del kernel de Linux no es un organismo
formal y, por lo tanto, no puede firmar cualquier acuerdo de no
divulgación. La comunidad del kernel es consciente de la naturaleza
delicada de tales problemas y ofrece un Memorando de Entendimiento en su
lugar.

Memorando de Entendimiento
--------------------------

La comunidad del kernel de Linux tiene una comprensión profunda del
requisito de mantener los problemas de seguridad de hardware bajo embargo
para la coordinación entre diferentes vendedores de OS, distribuidores,
vendedores de hardware y otras partes.

La comunidad del kernel de Linux ha manejado con éxito los problemas de
seguridad del hardware en el pasado y tiene los mecanismos necesarios para
permitir el desarrollo compatible con la comunidad bajo restricciones de
embargo.

La comunidad del kernel de Linux tiene un equipo de seguridad de hardware
dedicado para el contacto inicial, el cual supervisa el proceso de manejo
de tales problemas bajo las reglas de embargo.

El equipo de seguridad de hardware identifica a los desarrolladores
(expertos en dominio) que formarán el equipo de respuesta inicial para un
problema en particular. El equipo de respuesta inicial puede involucrar
más desarrolladores (expertos en dominio) para abordar el problema de la
mejor manera técnica.

Todos los desarrolladores involucrados se comprometen a adherirse a las
reglas del embargo y a mantener confidencial la información recibida. La
violación de la promesa conducirá a la exclusión inmediata del problema
actual y la eliminación de todas las listas de correo relacionadas.
Además, el equipo de seguridad de hardware también excluirá al
delincuente de problemas futuros. El impacto de esta consecuencia es un
elemento de disuasión altamente efectivo en nuestra comunidad. En caso de
que ocurra una violación, el equipo de seguridad de hardware informará a
las partes involucradas inmediatamente. Si usted o alguien tiene
conocimiento de una posible violación, por favor, infórmelo inmediatamente
a los oficiales de seguridad de hardware.

Proceso
^^^^^^^

Debido a la naturaleza distribuida globalmente del desarrollo del kernel
de Linux, las reuniones cara a cara hacen imposible abordar los
problemas de seguridad del hardware. Las conferencias telefónicas son
difíciles de coordinar debido a las zonas horarias y otros factores y
solo deben usarse cuando sea absolutamente necesario. El correo
electrónico encriptado ha demostrado ser el método de comunicación más
efectivo y seguro para estos tipos de problemas.

Inicio de la divulgación
""""""""""""""""""""""""

La divulgación comienza contactado al equipo de seguridad de hardware del
kernel de Linux por correo electrónico. Este contacto inicial debe
contener una descripción del problema y una lista de cualquier hardware
afectado conocido. Si su organización fabrica o distribuye el hardware
afectado, le animamos a considerar también que otro hardware podría estar
afectado.

El equipo de seguridad de hardware proporcionará una lista de correo
encriptada específica para el incidente que se utilizará para la discusión
inicial con el reportero, la divulgación adicional y la coordinación.

El equipo de seguridad de hardware proporcionará a la parte reveladora una
lista de desarrolladores (expertos de dominios) a quienes se debe informar
inicialmente sobre el problema después de confirmar con los
desarrolladores que se adherirán a este Memorando de Entendimiento y al
proceso documentado. Estos desarrolladores forman el equipo de respuesta
inicial y serán responsables de manejar el problema después del contacto
inicial. El equipo de seguridad de hardware apoyará al equipo de
respuesta, pero no necesariamente involucrandose en el proceso de desarrollo
de mitigación.

Si bien los desarrolladores individuales pueden estar cubiertos por un
acuerdo de no divulgación a través de su empleador, no pueden firmar
acuerdos individuales de no divulgación en su papel de desarrolladores
del kernel de Linux. Sin embargo, aceptarán adherirse a este proceso
documentado y al Memorando de Entendimiento.

La parte reveladora debe proporcionar una lista de contactos para todas
las demás entidades ya que han sido, o deberían ser, informadas sobre el
problema. Esto sirve para varios propósitos:

 - La lista de entidades divulgadas permite la comunicación en toda la
   industria, por ejemplo, otros vendedores de OS, vendedores de HW, etc.

 - Las entidades divulgadas pueden ser contactadas para nombrar a expertos
   que deben participar en el desarrollo de la mitigación.

 - Si un experto que se requiere para manejar un problema es empleado por
   una entidad cotizada o un miembro de una entidad cotizada, los equipos
   de respuesta pueden solicitar la divulgación de ese experto a esa
   entidad. Esto asegura que el experto también forme parte del equipo de
   respuesta de la entidad.

Divulgación
"""""""""""

La parte reveladora proporcionará información detallada al equipo de
respuesta inicial a través de la lista de correo encriptada especifica.

Según nuestra experiencia, la documentación técnica de estos problemas
suele ser un punto de partida suficiente y es mejor hacer aclaraciones
técnicas adicionales a través del correo electrónico.

Desarrollo de la mitigación
"""""""""""""""""""""""""""

El equipo de respuesta inicial configura una lista de correo encriptada o
reutiliza una existente si es apropiada.

El uso de una lista de correo está cerca del proceso normal de desarrollo
de Linux y se ha utilizado con éxito en el desarrollo de mitigación para
varios problemas de seguridad de hardware en el pasado.

La lista de correo funciona en la misma manera que el desarrollo normal de
Linux. Los parches se publican, discuten y revisan y, si se acuerda, se
aplican a un repositorio git no público al que solo pueden acceder los
desarrolladores participantes a través de una conexión segura. El
repositorio contiene la rama principal de desarrollo en comparación con
el kernel principal y las ramas backport para versiones estables del
kernel según sea necesario.

El equipo de respuesta inicial identificará a más expertos de la
comunidad de desarrolladores del kernel de Linux según sea necesario. La
incorporación de expertos puede ocurrir en cualquier momento del proceso
de desarrollo y debe manejarse de manera oportuna.

Si un experto es empleado por o es miembro de una entidad en la lista de
divulgación proporcionada por la parte reveladora, entonces se solicitará
la participación de la entidad pertinente.

Si no es así, entonces se informará a la parte reveladora sobre la
participación de los expertos. Los expertos están cubiertos por el
Memorando de Entendimiento y se solicita a la parte reveladora que
reconozca la participación. En caso de que la parte reveladora tenga una
razón convincente para objetar, entonces esta objeción debe plantearse
dentro de los cinco días laborables y resolverse con el equipo de
incidente inmediatamente. Si la parte reveladora no reacciona dentro de
los cinco días laborables, esto se toma como un reconocimiento silencioso.

Después del reconocimiento o la resolución de una objeción, el experto es
revelado por el equipo de incidente y se incorpora al proceso de
desarrollo.

Lanzamiento coordinado
""""""""""""""""""""""

Las partes involucradas negociarán la fecha y la hora en la que termina el
embargo. En ese momento, las mitigaciones preparadas se integran en los
árboles de kernel relevantes y se publican.

Si bien entendemos que los problemas de seguridad del hardware requieren
un tiempo de embargo coordinado, el tiempo de embargo debe limitarse al
tiempo mínimo que se requiere para que todas las partes involucradas
desarrollen, prueben y preparen las mitigaciones. Extender el tiempo de
embargo artificialmente para cumplir con las fechas de discusión de la
conferencia u otras razones no técnicas está creando más trabajo y carga
para los desarrolladores y los equipos de respuesta involucrados, ya que
los parches necesitan mantenerse actualizados para seguir el desarrollo en
curso del kernel upstream, lo cual podría crear cambios conflictivos.

Asignación de CVE
"""""""""""""""""

Ni el equipo de seguridad de hardware ni el equipo de respuesta inicial
asignan CVEs, ni se requieren para el proceso de desarrollo. Si los CVEs
son proporcionados por la parte reveladora, pueden usarse con fines de
documentación.

Embajadores del proceso
-----------------------

Para obtener asistencia con este proceso, hemos establecido embajadores
en varias organizaciones, que pueden responder preguntas o proporcionar
orientación sobre el proceso de reporte y el manejo posterior. Los
embajadores no están involucrados en la divulgación de un problema en
particular, a menos que lo solicite un equipo de respuesta o una parte
revelada involucrada. La lista de embajadores actuales:

  ============= ========================================================
  AMD		Tom Lendacky <thomas.lendacky@amd.com>
  Ampere	Darren Hart <darren@os.amperecomputing.com>
  ARM		Catalin Marinas <catalin.marinas@arm.com>
  IBM Power	Anton Blanchard <anton@linux.ibm.com>
  IBM Z		Christian Borntraeger <borntraeger@de.ibm.com>
  Intel		Tony Luck <tony.luck@intel.com>
  Qualcomm	Trilok Soni <quic_tsoni@quicinc.com>
  Samsung	Javier González <javier.gonz@samsung.com>

  Microsoft	James Morris <jamorris@linux.microsoft.com>
  Xen		Andrew Cooper <andrew.cooper3@citrix.com>

  Canonical	John Johansen <john.johansen@canonical.com>
  Debian	Ben Hutchings <ben@decadent.org.uk>
  Oracle	Konrad Rzeszutek Wilk <konrad.wilk@oracle.com>
  Red Hat	Josh Poimboeuf <jpoimboe@redhat.com>
  SUSE		Jiri Kosina <jkosina@suse.cz>

  Google	Kees Cook <keescook@chromium.org>

  LLVM		Nick Desaulniers <ndesaulniers@google.com>
  ============= ========================================================

Si quiere que su organización se añada a la lista de embajadores, por
favor póngase en contacto con el equipo de seguridad de hardware. El
embajador nominado tiene que entender y apoyar nuestro proceso
completamente y está idealmente bien conectado en la comunidad del kernel
de Linux.

Listas de correo encriptadas
----------------------------

Usamos listas de correo encriptadas para la comunicación. El principio de
funcionamiento de estas listas es que el correo electrónico enviado a la
lista se encripta con la llave PGP de la lista o con el certificado S/MIME
de la lista. El software de lista de correo descifra el correo electrónico
y lo vuelve a encriptar individualmente para cada suscriptor con la llave
PGP del suscriptor o el certificado S/MIME. Los detalles sobre el software
de la lista de correo y la configuración que se usa para asegurar la
seguridad de las listas y la protección de los datos se pueden encontrar
aquí: https://korg.wiki.kernel.org/userdoc/remail.

Llaves de lista
^^^^^^^^^^^^^^^

Para el contacto inicial, consulte :ref:`Contacto`. Para las listas de
correo especificas de incidentes, la llave y el certificado S/MIME se
envían a los suscriptores por correo electrónico desde la lista
especifica.

Suscripción a listas específicas de incidentes
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

La suscripción es manejada por los equipos de respuesta. Las partes
reveladas que quieren participar en la comunicación envían una lista de
suscriptores potenciales al equipo de respuesta para que el equipo de
respuesta pueda validar las solicitudes de suscripción.

Cada suscriptor necesita enviar una solicitud de suscripción al equipo de
respuesta por correo electrónico. El correo electrónico debe estar firmado
con la llave PGP del suscriptor o el certificado S/MIME. Si se usa una
llave PGP, debe estar disponible desde un servidor de llave publica y esta
idealmente conectada a la red de confianza PGP del kernel de Linux. Véase
también: https://www.kernel.org/signature.html.

El equipo de respuesta verifica que la solicitud del suscriptor sea válida
y añade al suscriptor a la lista. Después de la suscripción, el suscriptor
recibirá un correo electrónico de la lista que está firmado con la llave
PGP de la lista o el certificado S/MIME de la lista. El cliente de correo
electrónico del suscriptor puede extraer la llave PGP o el certificado
S/MIME de la firma, de modo que el suscriptor pueda enviar correo
electrónico encriptado a la lista.
