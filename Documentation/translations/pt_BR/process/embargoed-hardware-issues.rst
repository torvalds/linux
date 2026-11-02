.. SPDX-License-Identifier: GPL-2.0

Problemas de hardware sob embargo
=================================

Escopo
------

Problemas de hardware que resultam em problemas de segurança formam uma categoria
de bugs de segurança diferente dos bugs de software puros que afetam apenas o
kernel do Linux.

Problemas de hardware como Meltdown, Spectre, L1TF, etc., devem ser tratados
de maneira diferente porque geralmente afetam todos os Sistemas Operacionais ("OS")
e, portanto, exigem coordenação entre diferentes fornecedores de SO, distribuições,
fabricantes de silício, integradores de hardware e outras partes. Para alguns
dos problemas, as mitigações de software podem depender de atualizações de
microcódigo ou firmware, o que requer ainda mais coordenação.

.. _pt_BR_Contact:

Contato
-------

A equipe de segurança de hardware do kernel Linux é separada da equipe regular
de segurança do kernel Linux.

A equipe lida apenas com o desenvolvimento de correções para problemas de
segurança de hardware sob embargo. Relatos de bugs de segurança de software puro
no kernel Linux não são tratados por esta equipe, e o autor do relato será
orientado a contatar a equipe regular de segurança do kernel Linux
(:ref:`Documentation/admin-guide/ <securitybugs>`) em vez disso.

A equipe pode ser contatada por e-mail em <hardware-security@kernel.org>. Esta
é uma lista privada de oficiais de segurança que ajudarão você a coordenar uma
correção de acordo com o nosso processo documentado.

A lista é criptografada e o e-mail para a lista pode ser enviado criptografado
por PGP ou S/MIME, e deve ser assinado com a chave PGP ou certificado S/MIME do
autor do relato. A chave PGP e o certificado S/MIME da equipe estão disponíveis
nas seguintes URLs:

  - PGP: https://www.kernel.org/static/files/hardware-security.asc
  - S/MIME: https://www.kernel.org/static/files/hardware-security.crt

Embora os problemas de segurança de hardware sejam frequentemente tratados pelo
fabricante de silício afetado, nós acolhemos o contato de pesquisadores ou
indivíduos que tenham identificado uma falha potencial de hardware.

Oficiais de segurança de hardware
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

A equipe atual de oficiais de segurança de hardware:

  - Linus Torvalds (Fellow da Linux Foundation)
  - Greg Kroah-Hartman (Fellow da Linux Foundation)
  - Thomas Gleixner (Fellow da Linux Foundation)

Operação das listas de e-mail
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

As listas de e-mail criptografadas que são usadas em nosso processo são
hospedadas na infraestrutura de TI da Linux Foundation. Ao fornecer este
serviço, os membros da equipe de operações de TI da Linux Foundation têm,
tecnicamente, a capacidade de acessar as informações sob embargo, mas são
obrigados à confidencialidade por seu contrato de trabalho. O pessoal de TI
da Linux Foundation também é responsável por operar e gerenciar o restante da
infraestrutura do kernel.org.

O atual diretor de infraestrutura de projetos de TI da Linux Foundation é
Konstantin Ryabitsev.


Acordos de não divulgação
-------------------------

A equipe de segurança de hardware do kernel Linux não é um órgão formal e,
portanto, é incapaz de celebrar quaisquer acordos de não divulgação. A
comunidade do kernel está ciente da natureza sensível de tais problemas e
oferece um Memorando de Entendimento em vez disso.


Memorando de Entendimento
-------------------------

A comunidade do kernel Linux compreende profundamente a necessidade de manter
os problemas de segurança de hardware sob embargo para a coordenação entre
diferentes fornecedores de SO, distribuidores, fabricantes de silício e outras
partes.

A comunidade do kernel Linux lidou com sucesso com problemas de segurança de
hardware no passado e possui os mecanismos necessários para permitir o
desenvolvimento compatível com a comunidade sob restrições de embargo.

A comunidade do kernel Linux possui uma equipe dedicada de segurança de hardware
para o contato inicial, que supervisiona o processo de tratamento de tais
problemas sob as regras de embargo.

A equipe de segurança de hardware identifica os desenvolvedores (especialistas no
domínio) que formarão a equipe de resposta inicial para um problema específico.
A equipe de resposta inicial pode trazer outros desenvolvedores (especialistas no
domínio) para resolver o problema da melhor maneira técnica.

Todos os desenvolvedores envolvidos comprometem-se a aderir às regras de embargo
e a manter as informações recebidas em sigilo. A violação do compromisso levará à
exclusão imediata do problema atual e à remoção de todas as listas de e-mail
relacionadas. Além disso, a equipe de segurança de hardware também excluirá o
infrator de futuros problemas. O impacto dessa consequência é um impedimento
altamente eficaz em nossa comunidade. Caso ocorra uma violação, a equipe de
segurança de hardware informará as partes envolvidas imediatamente. Se você ou
qualquer outra pessoa tomar conhecimento de uma potencial violação, por favor,
relate-a imediatamente aos oficiais de segurança de hardware.


Processo
^^^^^^^^

Devido à natureza globalmente distribuída do desenvolvimento do kernel Linux,
reuniões presenciais são quase impossíveis para lidar com problemas de
segurança de hardware. Conferências telefônicas são difíceis de coordenar devido
a fusos horários e outros fatores, devendo ser usadas apenas quando estritamente
necessário. O e-mail criptografado tem se mostrado o método de comunicação mais
eficiente e seguro para esses tipos de problema.

Início da divulgação
"""""""""""""""""""""

A divulgação começa enviando um e-mail para a equipe de segurança de hardware
do kernel Linux, conforme a seção Contato acima. Este contato inicial deve
conter uma descrição do problema e uma lista de qualquer silício afetado
conhecido. Se a sua organização constrói ou distribui o hardware afetado,
incentivamos você a considerar também quais outros hardwares podem ser
afetados. A parte que faz a divulgação é responsável por contatar os
fabricantes de silício afetados em tempo hábil.

A equipe de segurança de hardware fornecerá uma lista de e-mail criptografada
específica para o incidente, que será usada para a discussão inicial com o
relator, divulgação posterior e coordenação de correções.

A equipe de segurança de hardware fornecerá à parte divulgadora uma lista de
desenvolvedores (especialistas no domínio) que devem ser informados inicialmente
sobre o problema após confirmar com os desenvolvedores que eles aderirão a
este Memorando de Entendimento e ao processo documentado. Esses desenvolvedores
formam a equipe de resposta inicial e serão responsáveis por lidar com o
problema após o contato inicial. A equipe de segurança de hardware apoia a
equipe de resposta, mas não está necessariamente envolvida no processo de
desenvolvimento de mitigações.

Embora desenvolvedores individuais possam estar cobertos por um acordo de não
divulgação por meio de seu empregador, eles não podem celebrar acordos
individuais de não divulgação em seu papel como desenvolvedores do kernel
Linux. No entanto, eles concordarão em aderir a este processo documentado e ao
Memorando de Entendimento.

A parte divulgadora deve fornecer uma lista de contatos para todas as outras
entidades que já foram, ou devem ser, informadas sobre o problema. Isso serve
a vários propósitos:

 - A lista de entidades informadas permite a comunicação em toda a
   indústria, por exemplo, outros fornecedores de SO, fornecedores de HW, etc.

 - As entidades informadas podem ser contatadas para indicar especialistas
   que devem participar do desenvolvimento da mitigação.

 - Se um especialista necessário para lidar com um problema for funcionário
   de uma entidade listada ou membro de uma entidade listada, as equipes de
   resposta podem solicitar a inclusão desse especialista por parte daquela
   entidade. Isso garante que o especialista também faça parte da equipe de
   resposta da entidade.

Divulgação
""""""""""

A parte divulgadora fornece informações detalhadas à equipe de resposta inicial
por meio da lista de e-mail criptografada específica.

A partir de nossa experiência, a documentação técnica desses problemas costuma
ser um ponto de partida suficiente, e esclarecimentos técnicos adicionais são
melhor feitos por e-mail.

Desenvolvimento de mitigações
""""""""""""""""""""""""""""""

A equipe de resposta inicial configura uma lista de e-mail criptografada ou
reaproveita uma já existente, se apropriado.

O uso de uma lista de e-mail é próximo ao processo normal de desenvolvimento
do Linux e tem sido usado com sucesso para desenvolver mitigações para vários
problemas de segurança de hardware no passado.

A lista de e-mail opera da mesma forma que o desenvolvimento normal do Linux.
Os patches são publicados, discutidos, revisados e, se aprovados, aplicados a
um repositório git não público que é acessível apenas aos desenvolvedores
participantes por meio de uma conexão segura. O repositório contém o ramo
(branch) principal de desenvolvimento contra o kernel mainline e ramos de
retroporte (backport) para versões estáveis do kernel conforme necessário.

A equipe de resposta inicial identificará outros especialistas da comunidade
de desenvolvedores do kernel Linux conforme necessário. Qualquer parte
envolvida pode sugerir a inclusão de outros especialistas, cada um dos quais
estará sujeito aos mesmos requisitos descritos acima.

A inclusão de especialistas pode ocorrer a qualquer momento no processo de
desenvolvimento e precisa ser tratada em tempo hábil.

Se um especialista for funcionário ou membro de uma entidade na lista de
divulgação fornecida pela parte divulgadora, a participação será solicitada
à entidade relevante.

Caso contrário, a parte divulgadora será informada sobre a participação
dos especialistas. Os especialistas são cobertos pelo Memorando de Entendimento
e a parte divulgadora é solicitada a reconhecer a participação deles. No caso
de a parte divulgadora ter um motivo convincente para se opor, qualquer
objeção deve ser levantada no prazo de cinco dias úteis e resolvida com a
equipe do incidente imediatamente. Se a parte divulgadora não reagir dentro
de cinco dias úteis, isso é considerado como reconhecimento tácito.

Após a equipe do incidente reconhecer ou resolver uma objeção, o especialista
é informado e integrado ao processo de desenvolvimento.

Os participantes da lista não podem se comunicar sobre o problema fora da
lista de e-mail privada. Os participantes da lista não podem usar nenhum
recurso compartilhado (por exemplo, fazendas de compilação do empregador,
sistemas de IC, etc.) ao trabalhar em patches.

Acesso antecipado
"""""""""""""""""

Os patches discutidos e desenvolvidos na lista não podem ser distribuídos a
nenhum indivíduo que não seja membro da equipe de resposta, nem a nenhuma outra
organização.

Para permitir que os fornecedores de silício afetados trabalhem com suas equipes
internas e parceiros da indústria em testes, validação e logística, a seguinte
exceção é fornecida:

    Representantes designados dos fornecedores de silício afetados têm permissão
    para repassar os patches a qualquer momento para a equipe de resposta do
    fornecedor de silício. O representante deve notificar a equipe de resposta
    do kernel sobre o repasse. O fornecedor de silício afetado deve possuir e
    manter seu próprio processo de segurança documentado para quaisquer patches
    compartilhados com sua equipe de resposta que seja consistente com esta
    política.

    A equipe de resposta do fornecedor de silício pode distribuir esses patches
    aos seus parceiros da indústria e às suas equipes internas sob o processo
    de segurança documentado do fornecedor de silício. O feedback dos parceiros
    da indústria retorna ao fornecedor de silício e é comunicado por ele à
    equipe de resposta do kernel.

    O repasse para a equipe de resposta do fornecedor de silício remove
    qualquer responsabilidade civil ou legal da equipe de resposta do kernel
    em relação à divulgação prematura que ocorra devido ao envolvimento das
    equipes internas ou parceiros da indústria do fornecedor de silício. O
    fornecedor de silício garante esta liberação de responsabilidade ao
    concordar com este processo.

Lançamento coordenado
"""""""""""""""""""""

As partes envolvidas negociarão a data e a hora em que o embargo termina. Nesse
ponto, as mitigações preparadas são publicadas nas árvores de kernel relevantes.
Não há processo de pré-notificação: as mitigações são publicadas publicamente e
disponibilizadas para todos ao mesmo tempo.

Embora entendamos que problemas de segurança de hardware exijam tempo de embargo
coordenado, o tempo de embargo deve ser restrito ao mínimo necessário para que
todas as partes envolvidas desenvolvam, testem e preparem suas mitigações.
Estender o tempo de embargo artificialmente para cumprir datas de palestras em
conferências ou outros motivos não técnicos cria mais trabalho e ônus para os
desenvolvedores e equipes de resposta envolvidos, pois os patches precisam ser
mantidos atualizados para acompanhar o desenvolvimento contínuo do kernel
upstream, o que pode criar alterações conflitantes.

Atribuição de CVE
""""""""""""""""""

Nem a equipe de segurança de hardware nem a equipe de resposta inicial atribuem
CVEs, nem os CVEs são necessários para o processo de desenvolvimento. Se os CVEs
forem fornecidos pela parte divulgadora, eles poderão ser usados para fins de
documentação.

Embaixadores do processo
------------------------

Para obter assistência com este processo, estabelecemos embaixadores em várias
organizações, que podem responder a perguntas sobre ou fornecer orientações
acerca do processo de relatórios e tratamento posterior. Os embaixadores não
estão envolvidos na divulgação de um problema específico, a menos que seja
solicitado por uma equipe de resposta ou por uma parte divulgada envolvida.
A lista atual de embaixadores:

  ============= ========================================================
  AMD           Tom Lendacky <thomas.lendacky@amd.com>
  Ampere        Darren Hart <darren@os.amperecomputing.com>
  ARM           Catalin Marinas <catalin.marinas@arm.com>
  IBM Power     Madhavan Srinivasan <maddy@linux.ibm.com>
  IBM Z         Christian Borntraeger <borntraeger@de.ibm.com>
  Intel         Tony Luck <tony.luck@intel.com>
  Qualcomm      Trilok Soni <quic_tsoni@quicinc.com>
  RISC-V        Palmer Dabbelt <palmer@dabbelt.com>
  Samsung       Javier González <javier.gonz@samsung.com>

  Microsoft     James Morris <jamorris@linux.microsoft.com>
  Xen           Andrew Cooper <andrew.cooper3@citrix.com>

  Canonical     John Johansen <john.johansen@canonical.com>
  Debian        Ben Hutchings <ben@decadent.org.uk>
  Oracle        Konrad Rzeszutek Wilk <konrad.wilk@oracle.com>
  Red Hat       Josh Poimboeuf <jpoimboe@redhat.com>
  SUSE          Jiri Kosina <jkosina@suse.cz>

  Google        Kees Cook <keescook@chromium.org>

  LLVM          Nick Desaulniers <ndesaulniers@google.com>
  ============= ========================================================

Se você quiser que sua organização seja adicionada à lista de embaixadores,
entre em contato com a equipe de segurança de hardware. O embaixador indicado
deve compreender e apoiar totalmente o nosso processo e, idealmente, estar bem
conectado na comunidade do kernel Linux.

Listas de e-mail criptografadas
-------------------------------

Usamos listas de e-mail criptografadas para comunicação. O princípio de
operação dessas listas é que o e-mail enviado para a lista é criptografado
com a chave PGP da lista ou com o certificado S/MIME da lista. O software
da lista de e-mail descriptografa o e-mail e o recriptografa individualmente
para cada assinante com a chave PGP ou certificado S/MIME do assinante.
Detalhes sobre o software da lista de e-mail e a configuração usada para
garantir a segurança das listas e a proteção dos dados podem ser encontrados
aqui: https://korg.wiki.kernel.org/userdoc/remail.

Listas de chaves
^^^^^^^^^^^^^^^^

Para o contato inicial, consulte a seção :ref:`pt_BR_Contact` acima. Para listas de
e-mail específicas de incidentes, a chave e o certificado S/MIME são transmitidos
aos assinantes por e-mail enviado a partir da lista específica.

Inscrição em listas específicas de incidentes
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

A inscrição em listas específicas de incidentes é gerenciada pelas equipes de
resposta. As partes informadas que desejam participar da comunicação enviam
uma lista de potenciais especialistas para a equipe de resposta, para que esta
possa validar as solicitações de inscrição.

Cada assinante precisa enviar uma solicitação de inscrição para a equipe de
resposta por e-mail. O e-mail deve estar assinado com a chave PGP ou o certificado
S/MIME do assinante. Se uma chave PGP for utilizada, ela deve estar disponível
em um servidor de chaves público e, idealmente, conectada à teia de confiança
(web of trust) PGP do kernel Linux. Veja também:
https://www.kernel.org/signature.html.

A equipe de resposta verifica se a solicitação do assinante é válida e o
adiciona à lista. Após a inscrição, o assinante receberá e-mails da lista de
e-mail que são assinados com a chave PGP da lista ou com o certificado S/MIME
da lista. O cliente de e-mail do assinante pode extrair a chave PGP ou o
certificado S/MIME da assinatura para que o assinante possa enviar e-mails
criptografados para a lista.